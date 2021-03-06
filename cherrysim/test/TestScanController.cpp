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
#include <CherrySimTester.h>
#include <CherrySimUtils.h>
#include <Node.h>
#include <ScanController.h>

static int current_node_idx;

static void simulateAndCheckScanning(int simulate_time, bool is_scanning_active, CherrySimTester &tester)
{
	tester.SimulateForGivenTime(simulate_time);
	current_node_idx = tester.sim->currentNode->index;
	tester.sim->setNode(0);
	ASSERT_TRUE(tester.sim->currentNode->state.scanningActive == is_scanning_active);
	tester.sim->setNode(current_node_idx);
}

static void simulateAndCheckWindow(int simulate_time, int window, CherrySimTester &tester)
{
	tester.SimulateForGivenTime(simulate_time);
	current_node_idx = tester.sim->currentNode->index;
	tester.sim->setNode(0);
	ASSERT_EQ(tester.sim->currentNode->state.scanWindowMs, window);
	tester.sim->setNode(current_node_idx);
}

static ScanJob * AddJob(ScanJob &scan_job, CherrySimTester &tester)
{
	ScanJob * p_job;
	current_node_idx = tester.sim->currentNode->index;
	tester.sim->setNode(0);
	p_job = tester.sim->currentNode->gs.scanController.AddJob(scan_job);
	tester.sim->setNode(current_node_idx);
	return p_job;
}

static void RemoveJob(ScanJob * p_scan_job, CherrySimTester &tester)
{
	current_node_idx = tester.sim->currentNode->index;
	tester.sim->setNode(0);
	tester.sim->currentNode->gs.scanController.RemoveJob(p_scan_job);

	tester.sim->setNode(current_node_idx);
}

static void ForceStopAllScanJobs(CherrySimTester &tester)
{
	current_node_idx = tester.sim->currentNode->index;
	tester.sim->setNode(0);
	for (int i = 0; i < tester.sim->currentNode->gs.scanController.GetAmountOfJobs(); i++)
	{
		tester.sim->currentNode->gs.scanController.RemoveJob(tester.sim->currentNode->gs.scanController.GetJob(i));
	}
	tester.sim->setNode(current_node_idx);
}

TEST(TestScanController, TestIfScannerGetsEnabled) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.terminalId = 1;
	testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	strcpy(tester.sim->nodes[0].nodeConfiguration, "prod_sink_nrf52");
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);

	ScanJob job;
	job.timeMode = ScanJobTimeMode::ENDLESS;
	job.interval = MSEC_TO_UNITS(100, UNIT_0_625_MS);
	job.window = MSEC_TO_UNITS(50, UNIT_0_625_MS);
	job.state = ScanJobState::ACTIVE;
	job.type = ScanState::CUSTOM;
	AddJob(job, tester);

	simulateAndCheckWindow(1000, 50, tester);
}

TEST(TestScanController, TestScannerStopsAfterTimeoutTime) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.terminalId = 1;
	testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	strcpy(tester.sim->nodes[0].nodeConfiguration, "prod_sink_nrf52");
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);
	ForceStopAllScanJobs(tester);

	ScanJob job;
	job.timeMode = ScanJobTimeMode::TIMED;
	job.timeLeftDs = SEC_TO_DS(10);
	job.interval = MSEC_TO_UNITS(50, UNIT_0_625_MS);
	job.window = MSEC_TO_UNITS(50, UNIT_0_625_MS);
	job.state = ScanJobState::ACTIVE;
	job.type = ScanState::CUSTOM;
	AddJob(job, tester);

 
	simulateAndCheckWindow(1000, 50, tester);
	simulateAndCheckScanning(10000, false, tester);
}

TEST(TestScanController, TestScannerChooseJobWithHighestDutyCycle) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.terminalId = 1;
	//testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	strcpy(tester.sim->nodes[0].nodeConfiguration, "prod_sink_nrf52");
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);
	ForceStopAllScanJobs(tester);

	ScanJob job;
	job.timeMode = ScanJobTimeMode::ENDLESS;
	job.interval = MSEC_TO_UNITS(100, UNIT_0_625_MS);
	job.window = MSEC_TO_UNITS(50, UNIT_0_625_MS);
	job.state = ScanJobState::ACTIVE;
	job.type = ScanState::CUSTOM;
	AddJob(job, tester);


	simulateAndCheckWindow(1000, 50, tester);

	job.window = MSEC_TO_UNITS(40, UNIT_0_625_MS);
	AddJob(job, tester);

	simulateAndCheckWindow(1000, 50, tester);

	job.window = MSEC_TO_UNITS(60, UNIT_0_625_MS);
	AddJob(job, tester);

	simulateAndCheckWindow(1000, 60, tester);

	job.timeMode = ScanJobTimeMode::TIMED;
	job.timeLeftDs = SEC_TO_DS(3);
	job.window = MSEC_TO_UNITS(70, UNIT_0_625_MS);
	AddJob(job, tester);

	simulateAndCheckWindow(1000, 70, tester);

	// previous job should timeout
	simulateAndCheckWindow(2000, 60, tester);
}

TEST(TestScanController, TestScannerWillStopOnceAllJobsTimeout) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.terminalId = 1;
	//testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	strcpy(tester.sim->nodes[0].nodeConfiguration, "prod_sink_nrf52");
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);
	ForceStopAllScanJobs(tester);

	ScanJob job;
	job.timeMode = ScanJobTimeMode::TIMED;
	job.timeLeftDs = SEC_TO_DS(10);
	job.interval = MSEC_TO_UNITS(100, UNIT_0_625_MS);
	job.window = MSEC_TO_UNITS(50, UNIT_0_625_MS);
	job.state = ScanJobState::ACTIVE;
	job.type = ScanState::CUSTOM;
	AddJob(job, tester);

	simulateAndCheckScanning(1000, true, tester);

	job.timeMode = ScanJobTimeMode::TIMED;
	job.timeLeftDs = SEC_TO_DS(10);
	AddJob(job, tester);

	simulateAndCheckScanning(9000, true, tester);
	simulateAndCheckScanning(1000, false, tester);
}

TEST(TestScanController, TestScannerWillStopOnceAllJobsAreDeleted) {
	CherrySimTesterConfig testerConfig = CherrySimTester::CreateDefaultTesterConfiguration();
	SimConfiguration simConfig = CherrySimTester::CreateDefaultSimConfiguration();
	simConfig.numNodes = 2;
	simConfig.terminalId = 1;
	//testerConfig.verbose = true;

	CherrySimTester tester = CherrySimTester(testerConfig, simConfig);
	strcpy(tester.sim->nodes[0].nodeConfiguration, "prod_sink_nrf52");
	tester.Start();

	tester.SimulateUntilClusteringDone(100 * 1000);
	ForceStopAllScanJobs(tester);

	ScanJob job;
	job.timeMode = ScanJobTimeMode::ENDLESS;
	job.interval = MSEC_TO_UNITS(100, UNIT_0_625_MS);
	job.window = MSEC_TO_UNITS(50, UNIT_0_625_MS);
	job.state = ScanJobState::ACTIVE;
	job.type = ScanState::CUSTOM;
	ScanJob * p_job_1 = AddJob(job, tester);


	simulateAndCheckScanning(1000, true, tester);
	ScanJob * p_job_2 = AddJob(job, tester);


	simulateAndCheckScanning(1000, true, tester); 
	RemoveJob(p_job_1, tester);

	simulateAndCheckScanning(1000, true, tester);
	RemoveJob(p_job_2, tester);

	simulateAndCheckScanning(1000, false, tester);
}