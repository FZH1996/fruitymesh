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

#pragma once
#include <stdint.h>

//Unsigned ints
typedef uint8_t u8;
typedef uint16_t u16;
typedef unsigned u32;   //This is not defined uint32_t because GCC defines uint32_t as unsigned long, 
						//which is a problem when working with printf placeholders.

//Signed ints
typedef int8_t i8;
typedef int16_t i16;
typedef int i32;		//This is not defined int32_t because GCC defines int32_t as long,
						//which is a problem when working with printf placeholders.

static_assert(sizeof(u8) == 1, "");
static_assert(sizeof(u16) == 2, "");
static_assert(sizeof(u32) == 4, "");

static_assert(sizeof(i8) == 1, "");
static_assert(sizeof(i16) == 2, "");
static_assert(sizeof(i32) == 4, "");

//Data types for the mesh
typedef u16 NetworkId;
typedef u16 NodeId;
typedef u32 ClusterId;
typedef i16 ClusterSize;

/*## Available Node ids #############################################################*/
// Refer to protocol specification @https://github.com/mwaylabs/fruitymesh/wiki/Protocol-Specification

constexpr NodeId NODE_ID_BROADCAST = 0; //A broadcast will be received by all nodes within one mesh
constexpr NodeId NODE_ID_DEVICE_BASE = 1; //Beginning from 1, we can assign nodeIds to individual devices
constexpr NodeId NODE_ID_DEVICE_BASE_SIZE = 1999;

constexpr NodeId NODE_ID_VIRTUAL_BASE = 2000; //Used to assign sub-addresses to connections that do not belong to the mesh but want to perform mesh activity. Used as a multiplier.
constexpr NodeId NODE_ID_GROUP_BASE = 20000; //Used to assign group ids to nodes. A node can take part in many groups at once
constexpr NodeId NODE_ID_GROUP_BASE_SIZE = 10000;

constexpr NodeId NODE_ID_LOCAL_LOOPBACK = 30000; //30000 is like a local loopback address that will only send to the current node,
constexpr NodeId NODE_ID_HOPS_BASE = 30000; //30001 sends to the local node and one hop further, 30002 two hops
constexpr NodeId NODE_ID_HOPS_BASE_SIZE = 1000;

constexpr NodeId NODE_ID_SHORTEST_SINK = 31000;
constexpr NodeId NODE_ID_ANYCAST_THEN_BROADCAST = 31001; //31001 will send the message to any one of the connected nodes and only that node will then broadcast this message

constexpr NodeId NODE_ID_APP_BASE = 32000; //Custom GATT services, connections with Smartphones, should use (APP_BASE + moduleId)
constexpr NodeId NODE_ID_APP_BASE_SIZE = 1000;

constexpr NodeId NODE_ID_GLOBAL_DEVICE_BASE = 33000; //Can be used to assign nodeIds that are valid organization wide (e.g. for assets)
constexpr NodeId NODE_ID_GLOBAL_DEVICE_BASE_SIZE = 7000;

constexpr NodeId NODE_ID_CLC_SPECIFIC = 40000; //see usage in CLC module
constexpr NodeId NODE_ID_RESERVED = 40001; //Yet unassigned nodIds
constexpr NodeId NODE_ID_INVALID = 0xFFFF; //Special node id that is used in error cases. It must never be used as an sender or receiver.

//Different types of supported BLE stacks, specific versions can be added later if necessary
enum class BleStackType {
	INVALID = 0,
	NRF_SD_130_ANY = 100,
	NRF_SD_132_ANY = 200,
	NRF_SD_140_ANY = 300
};

// Chipset group ids. These define what kind of chipset the firmware is running on
enum class Chipset : NodeId
{
	CHIP_INVALID = 0,
	CHIP_NRF51 = 20000,
	CHIP_NRF52 = 20001,
	CHIP_NRF52840 = 20015,
};

/*## Key Types #############################################################*/
//Types of keys used by the mesh and other modules
enum class FmKeyId : u32
{
	ZERO = 0,
	NODE = 1,
	NETWORK = 2,
	BASE_USER = 3,
	ORGANIZATION = 4,
	RESTRAINED = 5,
	USER_DERIVED_START = 10,
	USER_DERIVED_END = (UINT32_MAX / 2),
};

/*## Modules #############################################################*/
//The module ids are used to identify a module over the network
//Numbers below 150 are standard defined, numbers obove this range are free to use for custom modules
enum class ModuleId : u8 {
	//Standard modules
	NODE = 0, // Not a module per se, but why not let it send module messages
	ADVERTISING_MODULE = 1,
	SCANNING_MODULE = 2,
	STATUS_REPORTER_MODULE = 3,
	DFU_MODULE = 4,
	ENROLLMENT_MODULE = 5,
	IO_MODULE = 6,
	DEBUG_MODULE = 7,
	CONFIG = 8,
	//BOARD_CONFIG = 9, //deprecated as of 20.01.2020 (boardconfig is not a module anymore)
	MESH_ACCESS_MODULE = 10,
	//MANAGEMENT_MODULE=11, //deprecated as of 22.05.2019
	TESTING_MODULE = 12,
	BULK_MODULE = 13,

	//M-way Modules
	CLC_MODULE = 150,
	VS_MODULE = 151,
	ENOCEAN_MODULE = 152,
	ASSET_MODULE = 153,
	EINK_MODULE = 154,
	WM_MODULE = 155,

	//Other Modules
	MY_CUSTOM_MODULE = 200,
	PING_MODULE = 201,
	TEMPLATE_MODULE = 202,

	//Invalid Module: 0xFF is the flash memory default and is therefore invalid
	INVALID_MODULE = 255,
};



// The reason why the device was rebooted
enum class RebootReason : u8 {
	UNKNOWN = 0,
	HARDFAULT = 1,
	APP_FAULT = 2,
	SD_FAULT = 3,
	PIN_RESET = 4,
	WATCHDOG = 5,
	FROM_OFF_STATE = 6,
	LOCAL_RESET = 7,
	REMOTE_RESET = 8,
	ENROLLMENT = 9,
	PREFERRED_CONNECTIONS = 10,
	DFU = 11,
	MODULE_ALLOCATOR_OUT_OF_MEMORY = 12,
	MEMORY_MANAGEMENT = 13,
	BUS_FAULT = 14,
	USAGE_FAULT = 15,
	ENROLLMENT_REMOVE = 16,
	FACTORY_RESET_FAILED = 17,
	FACTORY_RESET_SUCCEEDED_FAILSAFE = 18,
	SET_SERIAL_SUCCESS = 19,
	SET_SERIAL_FAILED = 20,
	SEND_TO_BOOTLOADER = 21,
	UNKNOWN_BUT_BOOTED = 22,

	USER_DEFINED_START = 200,
	USER_DEFINED_END = 255,
};

/*############ Live Report types ################*/
//Live reports are sent through the mesh as soon as something notable happens
//Could be some info, a warning or an error

enum class LiveReportTypes : u8 {
	LEVEL_ERROR = 0,
	LEVEL_WARN = 50,
	HANDSHAKED_MESH_DISCONNECTED = 51, //extra is partnerid, extra2 is appDisconnectReason
	WARN_GAP_DISCONNECTED = 52, //extra is partnerid, extra2 is hci code

	//########
	LEVEL_INFO = 100,
	GAP_CONNECTED_INCOMING = 101, //extra is connHandle, extra2 is 4 bytes of gap addr
	GAP_TRYING_AS_MASTER = 102, //extra is partnerId, extra2 is 4 bytes of gap addr
	GAP_CONNECTED_OUTGOING = 103, //extra is connHandle, extra2 is 4 byte of gap addr
	//Deprecated: GAP_DISCONNECTED = 104,

	HANDSHAKE_FAIL = 105, //extra is tempPartnerId, extra2 is handshakeFailCode
	MESH_CONNECTED = 106, //extra is partnerid, extra2 is asWinner
	//Deprecated: MESH_DISCONNECTED = 107,

	//########
	LEVEL_DEBUG = 150,
	DECISION_RESULT = 151 //extra is decision type, extra2 is preferredPartner
};

enum class LiveReportHandshakeFailCode : u8
{
	SUCCESS,
	SAME_CLUSTERID,
	NETWORK_ID_MISMATCH,
	WRONG_DIRECTION,
	UNPREFERRED_CONNECTION
};

enum class SensorIdentifier : u16 {
	UNKNOWN = 0,
	BME280 = 1,
	LIS2DH12 = 2,
	TLV493D = 3,
	BMG250 = 4
};

struct SensorPins {
	SensorIdentifier sensorIdentifier = SensorIdentifier::UNKNOWN;
};

struct Bme280Pins : SensorPins {
	i32 misoPin = -1;
	i32 mosiPin = -1;
	i32 sckPin = -1;
	i32 ssPin = -1;
	i32 sensorEnablePin = -1;
	bool sensorEnablePinActiveHigh = true;
};

struct Tlv493dPins : SensorPins {
	i32 sckPin = -1;
	i32 sdaPin = -1;
	i32 sensorEnablePin = -1;
	i32 twiEnablePin = -1;
	bool sensorEnablePinActiveHigh = true;
	bool twiEnablePinActiveHigh = true;
};

struct Bmg250Pins : SensorPins {
	i32 sckPin = -1;
	i32 sdaPin = -1;
	i32 interrupt1Pin = -1;
	i32 sensorEnablePin = -1;//-1 if sensor enable pin is not present
	i32 twiEnablePin = -1; // -1 if twi enable pin is not present
	bool sensorEnablePinActiveHigh = true;
	bool twiEnablePinActiveHigh = true;
};

struct Lis2dh12Pins : SensorPins {
	i32 mosiPin = -1;
	i32 misoPin = -1;
	i32 sckPin = -1;
	i32 ssPin = -1;
	i32 sdaPin = -1;//if lis2dh12 is attached to twi interface then we use only sda and sck pin
	i32 interrupt1Pin = -1;//FiFo interrupt is on pin 1
	i32 interrupt2Pin = -1;//movement detection interrupt is on pin 2
	i32 sensorEnablePin = -1;//if equal to -1 means that enable Pin is not present
	bool sensorEnablePinActiveHigh = true;
};

//A struct that combines a data pointer and the accompanying length
struct SizedData {
	u8*		data; //Pointer to data
	u16		length; //Length of Data
};

template<typename T>
struct TwoDimStruct
{
	T x;
	T y;
};

template<typename T>
struct ThreeDimStruct
{
	T x;
	T y;
	T z;
};

// To determine from which location the node config was loaded
enum class DeviceConfigOrigins : u8 {
	RANDOM_CONFIG = 0,
	UICR_CONFIG = 1,
	TESTDEVICE_CONFIG = 2
};

// The different kind of nodes supported by FruityMesh
enum class DeviceType : u8 {
	INVALID = 0,
	STATIC = 1, // A normal node that remains static at one position
	ROAMING = 2, // A node that is moving constantly or often (not implemented)
	SINK = 3, // A static node that wants to acquire data, e.g. a MeshGateway
	ASSET = 4, // A roaming node that is sporadically or never connected but broadcasts data
	LEAF = 5  // A node that will never act as a slave but will only connect as a master (useful for roaming nodes, but no relaying possible)
};

// The different terminal modes
enum class TerminalMode : u8 {
	JSON = 0, //Interrupt based terminal input and blocking output
	PROMPT = 1, //blockin in and out with echo and backspace options
	DISABLED = 2 //Terminal is disabled, no in and output
};

//Enrollment states
enum class EnrollmentState : u8 {
	NOT_ENROLLED = 0,
	ENROLLED = 1
};

//These codes are returned from the PreEnrollmentHandler
enum class PreEnrollmentReturnCode : u8 {
	DONE = 0, //PreEnrollment of the Module was either not necessary or successfully done
	WAITING = 1, //PreEnrollment must do asynchronous work and will afterwards call the PreEnrollmentDispatcher
	FAILED = 2 //PreEnrollment was not successfuly, so enrollment should continue
};

//Used for intercepting messages befoure they are routed through the mesh
typedef u32 RoutingDecision;
constexpr RoutingDecision ROUTING_DECISION_BLOCK_TO_MESH = 0x1;
constexpr RoutingDecision ROUTING_DECISION_BLOCK_TO_MESH_ACCESS = 0x2;

//Defines the different scanning intervals for each state
enum class ScanState : u8 {
	LOW = 0,
	HIGH = 1,
	CUSTOM = 2,
};

// Mesh discovery states
enum class DiscoveryState : u8 {
	INVALID = 0,
	HIGH = 1, // Scanning and advertising at a high duty cycle
	LOW = 2, // Scanning and advertising at a low duty cycle
	OFF = 3, // Scanning and advertising not enabled by the node to save power (Other modules might still advertise or scan)
};

//All known Subtypes of BaseConnection supported by the ConnectionManager
enum class ConnectionType : u8 {
	INVALID = 0,
	FRUITYMESH = 1, // A mesh connection
	APP = 2, // Base class of a customer specific connection (deprecated)
	CLC_APP = 3,
	RESOLVER = 4, // Resolver connection used to determine the correct connection
	MESH_ACCESS = 5, // MeshAccessConnection
};

//This enum defines packet authorization for MeshAccessConnetions
//First auth is undetermined, then rights decrease until the last entry, biggest entry num has preference always
enum class MeshAccessAuthorization : u8 {
	UNDETERMINED = 0, //Packet was not checked by any module
	WHITELIST = 1, //Packet was whitelisted by a module
	LOCAL_ONLY = 2, //Packet must only be processed by the receiving node and not by the mesh
	BLACKLIST = 3, //Packet was blacklisted by a module (This always wins over whitelisted)
};

//Led mode that defines what the LED does (mainly for debugging)
enum class LedMode : u8 {
	OFF = 0, // Led is off
	ON = 1, // Led is constantly on
	CONNECTIONS = 2, // Led blinks red if not connected and green for the number of connections
	RADIO = 3, // Led shows radio activity
	CLUSTERING = 4, // Led colour chosen according to clusterId (deprecated)
	ASSET = 5,
	CUSTOM = 6, // Led controlled by a specific module
};

//DFU ERROR CODES
enum class DfuStartDfuResponseCode : u8
{
	OK = 0,
	SAME_VERSION = 1,
	RUNNING_NEWER_VERSION = 2,
	ALREADY_IN_PROGRESS = 3,
	NO_BOOTLOADER = 4,
	FLASH_BUSY = 5,
	NOT_ENOUGH_SPACE = 6,
	CHUNKS_TOO_BIG = 7,
	MODULE_NOT_AVAILABLE = 8,
	MODULE_NOT_UPDATABLE = 9,
	COMPONENT_NOT_UPDATEABLE = 10,
	MODULE_QUERY_WAITING = 11, //Special code that is used internally if a module queries another controller and continues the process later
	TOO_MANY_CHUNKS = 12,
};

enum class ErrorType : u32
{
	SUCCESS = 0,  ///< Successful command
	SVC_HANDLER_MISSING = 1,  ///< SVC handler is missing
	BLE_STACK_NOT_ENABLED = 2,  ///< Ble stack has not been enabled
	INTERNAL = 3,  ///< Internal Error
	NO_MEM = 4,  ///< No Memory for operation
	NOT_FOUND = 5,  ///< Not found
	NOT_SUPPORTED = 6,  ///< Not supported
	INVALID_PARAM = 7,  ///< Invalid Parameter
	INVALID_STATE = 8,  ///< Invalid state, operation disallowed in this state
	INVALID_LENGTH = 9,  ///< Invalid Length
	INVALID_FLAGS = 10, ///< Invalid Flags
	INVALID_DATA = 11, ///< Invalid Data
	DATA_SIZE = 12, ///< Data size exceeds limit
	TIMEOUT = 13, ///< Operation timed out
	NULL_ERROR = 14, ///< Null Pointer
	FORBIDDEN = 15, ///< Forbidden Operation
	INVALID_ADDR = 16, ///< Bad Memory Address
	BUSY = 17, ///< Busy
	CONN_COUNT = 18, ///< Connection Count exceeded
	RESOURCES = 19, ///< Not enough resources for operation
	UNKNOWN = 20,
	BLE_INVALID_CONN_HANDLE = 101,
	BLE_INVALID_ATTR_HANDLE = 102,
	BLE_NO_TX_PACKETS = 103
};

struct DeviceConfiguration {
	u32 magicNumber;           // must be set to 0xF07700 when UICR data is available
	u32 boardType;             // accepts an integer that defines the hardware board that fruitymesh should be running on
	u32 serialNumber[2];       // the given serial number (2 words)
	u32 nodeKey[4];            // randomly generated (4 words)
	u32 manufacturerId;        // set to manufacturer id according to the BLE company identifiers: https://www.bluetooth.org/en-us/specification/assigned-numbers/company-identifiers
	u32 defaultNetworkId;      // network id if preenrollment should be used
	u32 defualtNodeId;         // node id to be used if not enrolled
	u32 deviceType;            // type of device (sink, mobile, etc,..)
	u32 serialNumberIndex;     // unique index that represents the serial number
	u32 networkKey[4];         // default network key if preenrollment should be used (4 words)
};

//This struct represents the registers as dumped on the stack
//by ARM Cortex Hardware once a hardfault occurs
#pragma pack(push, 4)
struct stacked_regs_t
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t psr;
};
#pragma pack(pop)
