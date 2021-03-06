////////////////////////////////////////////////////////////////////////////////
// /****************************************************************************
// **
// ** Copyright (C) 2015-2020 M-Way Solutions GmbH
// ** Contact: https://www.blureange.io/licensing
// **
// ** This file is part of the Bluerange/FruityMesh implementation
// **
// ** $BR_BEGIN_LICENSE:GPL-EXCEPT$
// ** Commercial License Usage
// ** Licensees holding valid commercial Bluerange licenses may use this file in
// ** accordance with the commercial license agreement provided with the
// ** Software or, alternatively, in accordance with the terms contained in
// ** a written agreement between them and M-Way Solutions GmbH.
// ** For licensing terms and conditions see https://www.bluerange.io/terms-conditions. For further
// ** information use the contact form at https://www.bluerange.io/contact.
// **
// ** GNU General Public License Usage
// ** Alternatively, this file may be used under the terms of the GNU
// ** General Public License version 3 as published by the Free Software
// ** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
// ** included in the packaging of this file. Please review the following
// ** information to ensure the GNU General Public License requirements will
// ** be met: https://www.gnu.org/licenses/gpl-3.0.html.
// **
// ** $BR_END_LICENSE$
// **
// ****************************************************************************/
////////////////////////////////////////////////////////////////////////////////
#include "gtest/gtest.h"
#include "Utility.h"
#include "CherrySimTester.h"
#include "CherrySimUtils.h"
#include "Logger.h"
#include <json.hpp>

using json = nlohmann::json;

TEST(TestStatusReporterModule, TestCommands) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.terminalId = 0;
	//testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);

	tester.sim->findNodeById(1)->gs.logger.enableTag("DEBUGMOD");
	tester.sim->findNodeById(2)->gs.logger.enableTag("DEBUGMOD");

	tester.SendTerminalCommand(1, "action 2 status get_status");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"nodeId\":2,\"type\":\"status\",\"module\":3");


	tester.SendTerminalCommand(1, "action 2 status get_device_info");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"nodeId\":2,\"type\":\"device_info\",\"module\":3,");

	tester.SendTerminalCommand(1, "action 2 status get_connections");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"type\":\"connections\",\"nodeId\":2,\"module\":3,\"partners\":[");

	tester.SendTerminalCommand(1, "action 2 status get_nearby");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"nodeId\":2,\"type\":\"nearby_nodes\",\"module\":3,\"nodes\":[");

	tester.SendTerminalCommand(1, "action 2 status set_init");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"type\":\"set_init_result\",\"nodeId\":2,\"module\":3}");

	//tester.SendTerminalCommand(1, "action 2 status keep_alive"); //TODO: Hard to test!
	//tester.SimulateUntilMessageReceived(10 * 1000, 1, "TODO"); 

	tester.SendTerminalCommand(1, "action 2 status get_errors");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"type\":\"error_log_entry\",\"nodeId\":2,\"module\":3,");

	tester.SendTerminalCommand(1, "action 2 status livereports 42");
	tester.SimulateUntilMessageReceived(10 * 1000, 2, "LiveReporting is now 42");

	tester.SendTerminalCommand(1, "action 2 status get_rebootreason");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"type\":\"reboot_reason\",\"nodeId\":2,\"module\":3,");
}

#ifndef GITHUB_RELEASE
TEST(TestStatusReporterModule, TestHopsToSinkFixing) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 6;
	simConfig.terminalId = 0;
	//testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);

	for (int i = 1; i <= 6; i++) tester.sim->findNodeById(i)->gs.logger.enableTag("DEBUGMOD");

	MeshConnections inConnections = tester.sim->findNodeById(2)->gs.cm.GetMeshConnections(ConnectionDirection::DIRECTION_IN);
	MeshConnections outConnections = tester.sim->findNodeById(2)->gs.cm.GetMeshConnections(ConnectionDirection::DIRECTION_OUT);
	u16 invalidHops = simConfig.numNodes + 10;   // a random value that is not possible to be correct
	u16 validHops = simConfig.numNodes - 1;	// initialize to max number of hops

	// set all inConnections for node 2 to invalid and find the one with least hops to sink
	for (int i = 0; i < inConnections.count; i++) {
		u16 tempHops;
		tempHops = inConnections.connections[i]->getHopsToSink();
		if (tempHops < validHops) validHops = tempHops;
		inConnections.connections[i]->setHopsToSink(invalidHops);
	}

	// set all outConnections for node 2 to invalid and find the one with least hops to sink
	for (int i = 0; i < outConnections.count; i++) {
		u16 tempHops;
		tempHops = inConnections.connections[i]->getHopsToSink();
		if (tempHops < validHops) validHops = tempHops;
		outConnections.connections[i]->setHopsToSink(invalidHops);
	}

	tester.SendTerminalCommand(1, "action max_hops status keep_alive");
	tester.SimulateForGivenTime(1000 * 10);

	// get_erros will collect errors from the node but will also clear them
	tester.SendTerminalCommand(1, "action 2 status get_errors");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"type\":\"error_log_entry\",\"nodeId\":2,\"module\":3,\"errType\":2,\"code\":44,\"extra\":%u", invalidHops);


	tester.SendTerminalCommand(1, "action max_hops status keep_alive");
	tester.SimulateForGivenTime(1000 * 10);

	tester.SendTerminalCommand(1, "action 2 status get_errors");
	tester.SimulateUntilMessageReceived(10 * 1000, 1, "{\"type\":\"error_log_entry\",\"nodeId\":2,\"module\":3,");
	// We expect that incorrect hops error wont be received as hopsToSink should have been fixed together with first keep_alive message.
	{
		Exceptions::DisableDebugBreakOnException disabler;
		ASSERT_THROW(tester.SimulateUntilMessageReceived(10 * 1000, 1, "\"errType\":%u,\"code\":%u", LoggingError::CUSTOM, CustomErrorTypes::FATAL_INCORRECT_HOPS_TO_SINK), TimeoutException);
	}
}
#endif //GITHUB_RELEASE

#ifndef GITHUB_RELEASE
TEST(TestStatusReporterModule, TestConnectionRssiReportingWithoutNoise) {
	//Test Rssi reporting when RssiNoise is disabled 
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	//testerConfig.verbose = true;
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.rssiNoise = false;
	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	tester.Start();

	tester.SimulateUntilClusteringDone(10 * 1000);

	tester.SimulateGivenNumberOfSteps(100);

	int rssiCalculated = (int)tester.sim->GetReceptionRssi(tester.sim->findNodeById(1), tester.sim->findNodeById(2));

	tester.SendTerminalCommand(1, "action 1 status get_connections");

	//Wait for the message of reported rssi
	std::vector<SimulationMessage> message = {
		SimulationMessage(1,"{\"type\":\"connections\",\"nodeId\":1,\"module\":3,\"partners\":[")
	};

	tester.SimulateUntilMessagesReceived(10 * 100, message);

	const std::string messageComplete = message[0].getCompleteMessage();

	//parse rssi value
	auto j = json::parse(messageComplete);
	int rssiReported = j["/rssiValues/1"_json_pointer].get<int>();


	/*Check if the reported RSSI is equal to calculated one when rssi noise is inactive*/
	if (rssiReported != rssiCalculated) {
		FAIL() << "RSSI calculated is not equal to RSSI reported";
	}
}
#endif //GITHUB_RELEASE

#ifndef GITHUB_RELEASE
TEST(TestStatusReporterModule, TestConnectionRssiReportingWithNoise) {
	//Test Rssi reporting when RssiNoise is disabled 
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	//testerConfig.verbose = true;
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.rssiNoise = true;
	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	tester.Start();

	tester.SimulateUntilClusteringDone(10 * 1000);

	tester.SimulateGivenNumberOfSteps(100);

	int rssiCalculated = (int)tester.sim->GetReceptionRssi(tester.sim->findNodeById(1), tester.sim->findNodeById(2));

	tester.SendTerminalCommand(1, "action 1 status get_connections");

	//Wait for the message of reported rssi
	std::vector<SimulationMessage> message = {
		SimulationMessage(1,"{\"type\":\"connections\",\"nodeId\":1,\"module\":3,\"partners\":[")
	};

	tester.SimulateUntilMessagesReceived(10 * 100, message);

	const std::string messageComplete = message[0].getCompleteMessage();

	//parse rssi value
	auto j = json::parse(messageComplete);
	int rssiReported = j["/rssiValues/1"_json_pointer].get<int>();


	/*Check if the reported RSSI is equal to calculated one when rssi noise is inactive*/
	if (std::abs(rssiReported - rssiCalculated) > 6) {
		FAIL() << "RSSI calculated is not nearly equal to RSSI reported";
	}
}
#endif //GITHUB_RELEASE
