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

/*
 * This is the HAL for the NRF51 and NRF52 chipsets
 * It is also the HAL used by the CherrySim simulator
 */


#include "FruityHal.h"
#include "FruityMesh.h"
#include <FruityHalNrf.h>
#include <types.h>
#include <GlobalState.h>
#include <Logger.h>
#include <ScanController.h>
#include <Node.h>
#include "Utility.h"
#ifdef SIM_ENABLED
#include <CherrySim.h>
#endif
#ifndef GITHUB_RELEASE
#if IS_ACTIVE(CLC_MODULE)
#include <ClcComm.h>
#endif
#if IS_ACTIVE(VS_MODULE)
#include <VsComm.h>
#endif
#if IS_ACTIVE(WM_MODULE)
#include <WmComm.h>
#endif
#endif //GITHUB_RELEASE

extern "C" {
#ifndef SIM_ENABLED
#include <app_util_platform.h>
#include <nrf_uart.h>
#include <nrf_mbr.h>
#ifdef NRF52
#include <nrf_power.h>
#include <nrf_drv_twi.h>
#include <nrf_drv_spi.h>
#include <nrf_drv_saadc.h>
#include <nrf_sdh.h>
#include <nrf_sdh_ble.h>
#include <nrf_sdh_soc.h>
#else
#include <nrf_drv_adc.h>
#endif
#if IS_ACTIVE(VIRTUAL_COM_PORT)
#include <virtual_com_port.h>
#endif
#endif
}

#define APP_SOC_OBSERVER_PRIO           1
#define APP_BLE_OBSERVER_PRIO           2

#if defined(NRF51)
#define BOOTLOADER_UICR_ADDRESS           (NRF_UICR->BOOTLOADERADDR)
#elif defined(NRF52) || defined(NRF52840)
#define BOOTLOADER_UICR_ADDRESS           (NRF_UICR->NRFFW[0])
#elif defined(SIM_ENABLED)
#define BOOTLOADER_UICR_ADDRESS           (FLASH_REGION_START_ADDRESS + NRF_UICR->BOOTLOADERADDR)
#endif

#define APP_TIMER_PRESCALER     0 // Value of the RTC1 PRESCALER register
#define APP_TIMER_OP_QUEUE_SIZE 1 //Size of timer operation queues

#define APP_TIMER_MAX_TIMERS    5 //Maximum number of simultaneously created timers (2 + BSP_APP_TIMERS_NUMBER)

constexpr u8 MAX_GPIOTE_HANDLERS = 4;
struct GpioteHandlerValues
{
	FruityHal::GpioInterruptHandler handler;
	u32 pin;
};

struct NrfHalMemory
{
#ifndef SIM_ENABLED
	SimpleArray <app_timer_t, APP_TIMER_MAX_TIMERS> swTimers;
	ble_db_discovery_t discoveredServices;
	volatile bool twiXferDone;
	bool twiInitDone;
	volatile bool spiXferDone;
	bool spiInitDone;
#endif
	GpioteHandlerValues GpioHandler[MAX_GPIOTE_HANDLERS];
	u8 gpioteHandlersCreated;
	ble_evt_t const * currentEvent;
	u8 timersCreated;
};




//This tag is used to set the SoftDevice configuration and must be used when advertising or creating connections
constexpr int BLE_CONN_CFG_TAG_FM = 1;

constexpr int BLE_CONN_CFG_GAP_PACKET_BUFFERS = 7;

//Bootloader constants
constexpr int BOOTLOADER_DFU_START  = (0xB1);      /**< Value to set in GPREGRET to boot to DFU mode. */
constexpr int BOOTLOADER_DFU_START2 = (0xB2);      /**< Value to set in GPREGRET2 to boot to DFU mode. */

#include <string.h>

//Forward declarations
static ErrorType nrfErrToGeneric(u32 code);
static FruityHal::BleGattEror nrfErrToGenericGatt(u32 code);


#define __________________BLE_STACK_INIT____________________
// ############### BLE Stack Initialization ########################

static u32 getramend(void)
{
	u32 ram_total_size;

#if defined(NRF51) || defined(SIM_ENABLED)
	u32 block_size = NRF_FICR->SIZERAMBLOCKS;
	ram_total_size = block_size * NRF_FICR->NUMRAMBLOCK;
#else
	ram_total_size = NRF_FICR->INFO.RAM * 1024;
#endif

	return 0x20000000 + ram_total_size;
}

static inline FruityHal::BleGattEror nrfErrToGenericGatt(u32 code)
{
	if (code == BLE_GATT_STATUS_SUCCESS)
	{
		return FruityHal::BleGattEror::SUCCESS;
	}
	else if ((code == BLE_GATT_STATUS_ATTERR_INVALID) || (code == BLE_GATT_STATUS_ATTERR_INVALID_HANDLE))
	{
		return FruityHal::BleGattEror::UNKNOWN;
	}
	else
	{
		return FruityHal::BleGattEror(code - 0x0100);
	}
}

static inline ErrorType nrfErrToGeneric(u32 code)
{
	//right now generic error has the same meaning and numering
	//FIXME: This is not true
	if (code <= NRF_ERROR_RESOURCES) return (ErrorType)code;
	else{
		switch(code)
		{
			case BLE_ERROR_INVALID_CONN_HANDLE:
				return ErrorType::BLE_INVALID_CONN_HANDLE;
			case BLE_ERROR_INVALID_ATTR_HANDLE:
				return ErrorType::BLE_INVALID_ATTR_HANDLE;
#ifdef NRF51
			case BLE_ERROR_NO_TX_PACKETS:
				return ErrorType::BLE_NO_TX_PACKETS;
#endif
			default:
				return ErrorType::UNKNOWN;
		}
	}
}

static inline nrf_gpio_pin_pull_t GenericPullModeToNrf(FruityHal::GpioPullMode mode)
{
	if (mode == FruityHal::GpioPullMode::GPIO_PIN_PULLUP) return NRF_GPIO_PIN_PULLUP;
	else if (mode == FruityHal::GpioPullMode::GPIO_PIN_PULLDOWN) return NRF_GPIO_PIN_PULLDOWN;
	else return NRF_GPIO_PIN_NOPULL;
}

static inline nrf_gpiote_polarity_t GenericPolarityToNrf(FruityHal::GpioTransistion transition)
{
	if (transition == FruityHal::GpioTransistion::GPIO_TRANSITION_TOGGLE) return NRF_GPIOTE_POLARITY_TOGGLE;
	else if (transition == FruityHal::GpioTransistion::GPIO_TRANSITION_LOW_TO_HIGH) return NRF_GPIOTE_POLARITY_LOTOHI;
	else return NRF_GPIOTE_POLARITY_HITOLO;
}

static inline FruityHal::GpioTransistion NrfPolarityToGeneric(nrf_gpiote_polarity_t polarity)
{
	if (polarity == NRF_GPIOTE_POLARITY_TOGGLE) return FruityHal::GpioTransistion::GPIO_TRANSITION_TOGGLE;
	else if (polarity == NRF_GPIOTE_POLARITY_LOTOHI) return FruityHal::GpioTransistion::GPIO_TRANSITION_LOW_TO_HIGH;
	else return FruityHal::GpioTransistion::GPIO_TRANSITION_HIGH_TO_LOW;
}

#ifdef NRF52
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context);
static void soc_evt_handler(uint32_t evt_id, void * p_context);

#endif

ErrorType FruityHal::BleStackInit()
{
	u32 err = 0;

	//Hotfix for NRF52 MeshGW v3 boards to disable NFC and use GPIO Pins 9 and 10
#ifdef CONFIG_NFCT_PINS_AS_GPIOS
	if (NRF_UICR->NFCPINS == 0xFFFFFFFF)
	{
		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
		NRF_UICR->NFCPINS = 0xFFFFFFFEUL;
		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}

		NVIC_SystemReset();
	}
#endif

	logt("NODE", "Init Softdevice version 0x%x, Boardid %d", 3, Boardconfig->boardType);

//TODO: Would be better to get the NRF52 part working in the simulator, here is a reduced version for the simulator
#ifdef SIM_ENABLED
	ble_enable_params_t params;
	CheckedMemset(&params, 0x00, sizeof(params));

	//Configure the number of connections as peripheral and central
	params.gap_enable_params.periph_conn_count = GS->config.totalInConnections; //Number of connections as Peripheral
	params.gap_enable_params.central_conn_count = GS->config.totalOutConnections; //Number of connections as Central

	err = sd_ble_enable(&params, nullptr);
#endif

#if defined(NRF51)
	// Initialize the SoftDevice handler with the low frequency clock source
	//And a reference to the previously allocated buffer
	//No event handler is given because the event handling is done in the main loop
	nrf_clock_lf_cfg_t clock_lf_cfg;

	if(Boardconfig->lfClockSource == NRF_CLOCK_LF_SRC_XTAL){
		clock_lf_cfg.source = NRF_CLOCK_LF_SRC_XTAL;
		clock_lf_cfg.rc_ctiv = 0;
	} else {
		clock_lf_cfg.source = NRF_CLOCK_LF_SRC_RC;
		clock_lf_cfg.rc_ctiv = 1;
	}
	clock_lf_cfg.rc_temp_ctiv = 0;
	clock_lf_cfg.xtal_accuracy = NRF_CLOCK_LF_XTAL_ACCURACY_100_PPM;

	err = softdevice_handler_init(&clock_lf_cfg, GS->currentEventBuffer, GlobalState::SIZE_OF_EVENT_BUFFER, nullptr);
	APP_ERROR_CHECK(err);

	logt("NODE", "Softdevice Init OK");

	//We now configure the parameters for enabling the softdevice, this will determine the needed RAM for the SD
	ble_enable_params_t params;
	CheckedMemset(&params, 0x00, sizeof(params));

	//Configre the number of Vendor Specific UUIDs
	params.common_enable_params.vs_uuid_count = 5;

	//Configure the number of connections as peripheral and central
	params.gap_enable_params.periph_conn_count = Conf::totalInConnections; //Number of connections as Peripheral
	params.gap_enable_params.central_conn_count = Conf::totalOutConnections; //Number of connections as Central
	params.gap_enable_params.central_sec_count = 1; //this application only needs to be able to pair in one central link at a time

	//Configure Bandwidth (We want all our connections to have a high throughput for RX and TX
	ble_conn_bw_counts_t bwCounts;
	CheckedMemset(&bwCounts, 0x00, sizeof(ble_conn_bw_counts_t));
	bwCounts.rx_counts.high_count = Conf::totalInConnections + Conf::totalOutConnections;
	bwCounts.tx_counts.high_count = Conf::totalInConnections + Conf::totalOutConnections;
	params.common_enable_params.p_conn_bw_counts = &bwCounts;

	//Configure the GATT Server Parameters
	params.gatts_enable_params.service_changed = 1; //we require the Service Changed characteristic
	params.gatts_enable_params.attr_tab_size = BLE_GATTS_ATTR_TAB_SIZE_DEFAULT; //the default Attribute Table size is appropriate for our application

	//The base ram address is gathered from the linker
	uint32_t app_ram_base = (u32)__application_ram_start_address;
	/* enable the BLE Stack */
	logt("NODE", "Ram base at 0x%x", (u32)app_ram_base);
	err = sd_ble_enable(&params, &app_ram_base);
	if(err == NRF_SUCCESS){
	/* Verify that __LINKER_APP_RAM_BASE matches the SD calculations */
		if(app_ram_base != (u32)__application_ram_start_address){
			logt("WARNING", "Warning: unused memory: 0x%x", ((u32)__application_ram_start_address) - (u32)app_ram_base);
		}
	} else if(err == NRF_ERROR_NO_MEM) {
		/* Not enough memory for the SoftDevice. Use output value in linker script */
		logt("ERROR", "Fatal: Not enough memory for the selected configuration. Required:0x%x", (u32)app_ram_base);
	} else {
		APP_ERROR_CHECK(err); //OK
	}

	//After the SoftDevice was enabled, we need to specifiy that we want a high bandwidth configuration
	// for both peripheral and central (6 packets for connInterval
	ble_opt_t opt;
	CheckedMemset(&opt, 0x00, sizeof(opt));
	opt.common_opt.conn_bw.role               = BLE_GAP_ROLE_PERIPH;
	opt.common_opt.conn_bw.conn_bw.conn_bw_rx = BLE_CONN_BW_HIGH;
	opt.common_opt.conn_bw.conn_bw.conn_bw_tx = BLE_CONN_BW_HIGH;
	err = sd_ble_opt_set(BLE_COMMON_OPT_CONN_BW, &opt);
	if(err != 0) logt("ERROR", "could not set bandwith %u", err);

	CheckedMemset(&opt, 0x00, sizeof(opt));
	opt.common_opt.conn_bw.role               = BLE_GAP_ROLE_CENTRAL;
	opt.common_opt.conn_bw.conn_bw.conn_bw_rx = BLE_CONN_BW_HIGH;
	opt.common_opt.conn_bw.conn_bw.conn_bw_tx = BLE_CONN_BW_HIGH;
	err = sd_ble_opt_set(BLE_COMMON_OPT_CONN_BW, &opt);
	if(err != 0) logt("ERROR", "could not set bandwith %u", err);

#elif defined(NRF52)

	//######### Enables the Softdevice
	u32 finalErr = 0;

	err = nrf_sdh_enable_request();
	APP_ERROR_CHECK(err);

	//Use IRQ Priority 7 for SOC and BLE events instead of 6
	//This allows us to use IRQ Prio 6 for UART and other peripherals
	err = sd_nvic_SetPriority(SD_EVT_IRQn, 7);
	APP_ERROR_CHECK(err);

	logt("ERROR", "ENReq %u", err);

	uint32_t ram_start = (u32)__application_ram_start_address;

	//######### Sets our custom SoftDevice configuration

	//Create a SoftDevice configuration
	ble_cfg_t ble_cfg;

	// Configure the connection count.
	CheckedMemset(&ble_cfg, 0, sizeof(ble_cfg));
	ble_cfg.conn_cfg.conn_cfg_tag                     = BLE_CONN_CFG_TAG_FM;
	ble_cfg.conn_cfg.params.gap_conn_cfg.conn_count   = Conf::totalInConnections + Conf::totalOutConnections;
	ble_cfg.conn_cfg.params.gap_conn_cfg.event_length = 4; //4 units = 5ms (1.25ms steps) this is the time used to handle one connection

	err = sd_ble_cfg_set(BLE_CONN_CFG_GAP, &ble_cfg, ram_start);
	if(err){
		if(finalErr == 0) finalErr = err;
		logt("ERROR", "BLE_CONN_CFG_GAP %u", err);
	}

	// Configure the connection roles.
	CheckedMemset(&ble_cfg, 0, sizeof(ble_cfg));
	ble_cfg.gap_cfg.role_count_cfg.periph_role_count  = Conf::totalInConnections;
	ble_cfg.gap_cfg.role_count_cfg.central_role_count = Conf::totalOutConnections;
	ble_cfg.gap_cfg.role_count_cfg.central_sec_count  = BLE_GAP_ROLE_COUNT_CENTRAL_SEC_DEFAULT; //TODO: Could change this

	err = sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT, &ble_cfg, ram_start);
	if(err){
		if(finalErr == 0) finalErr = err;
		logt("ERROR", "BLE_GAP_CFG_ROLE_COUNT %u", err);
	}

	// set HVN queue size
	CheckedMemset(&ble_cfg, 0, sizeof(ble_cfg_t));
	ble_cfg.conn_cfg.conn_cfg_tag = BLE_CONN_CFG_TAG_FM;
	ble_cfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = BLE_CONN_CFG_GAP_PACKET_BUFFERS; /* application wants to queue 7 HVNs */
	sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &ble_cfg, ram_start);

	// set WRITE_CMD queue size
	CheckedMemset(&ble_cfg, 0, sizeof(ble_cfg_t));
	ble_cfg.conn_cfg.conn_cfg_tag = BLE_CONN_CFG_TAG_FM;
	ble_cfg.conn_cfg.params.gattc_conn_cfg.write_cmd_tx_queue_size = BLE_CONN_CFG_GAP_PACKET_BUFFERS;
	sd_ble_cfg_set(BLE_CONN_CFG_GATTC, &ble_cfg, ram_start);

	// Configure the maximum ATT MTU.
	CheckedMemset(&ble_cfg, 0x00, sizeof(ble_cfg));
	ble_cfg.conn_cfg.conn_cfg_tag                 = BLE_CONN_CFG_TAG_FM;
	ble_cfg.conn_cfg.params.gatt_conn_cfg.att_mtu = NRF_SDH_BLE_GATT_MAX_MTU_SIZE;

	err = sd_ble_cfg_set(BLE_CONN_CFG_GATT, &ble_cfg, ram_start);
	APP_ERROR_CHECK(err);

	// Configure number of custom UUIDS.
	CheckedMemset(&ble_cfg, 0, sizeof(ble_cfg));
	ble_cfg.common_cfg.vs_uuid_cfg.vs_uuid_count = BLE_UUID_VS_COUNT_DEFAULT; //TODO: look in implementation

	err = sd_ble_cfg_set(BLE_COMMON_CFG_VS_UUID, &ble_cfg, ram_start);
	if(err){
		if(finalErr == 0) finalErr = err;
		logt("ERROR", "BLE_COMMON_CFG_VS_UUID %u", err);
	}

	// Configure the GATTS attribute table.
	CheckedMemset(&ble_cfg, 0x00, sizeof(ble_cfg));
	ble_cfg.gatts_cfg.attr_tab_size.attr_tab_size = NRF_SDH_BLE_GATTS_ATTR_TAB_SIZE;

	err = sd_ble_cfg_set(BLE_GATTS_CFG_ATTR_TAB_SIZE, &ble_cfg, ram_start);
	if(err){
		if(finalErr == 0) finalErr = err;
		logt("ERROR", "BLE_GATTS_CFG_ATTR_TAB_SIZE %u", err);
	}

	// Configure Service Changed characteristic.
	CheckedMemset(&ble_cfg, 0x00, sizeof(ble_cfg));
	ble_cfg.gatts_cfg.service_changed.service_changed = BLE_GATTS_SERVICE_CHANGED_DEFAULT;

	err = sd_ble_cfg_set(BLE_GATTS_CFG_SERVICE_CHANGED, &ble_cfg, ram_start);
	if(err){
		if(finalErr == 0) finalErr = err;
		logt("ERROR", "BLE_GATTS_CFG_SERVICE_CHANGED %u", err);
	}

	//######### Enables the BLE stack
	err = nrf_sdh_ble_enable(&ram_start);
	logt("ERROR", "Err %u, Linker Ram section should be at %x, len %x", err, (u32)ram_start, (u32)(getramend() - ram_start));
	APP_ERROR_CHECK(finalErr);
	APP_ERROR_CHECK(err);

	// Register a handler for BLE events.
	NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
	NRF_SDH_SOC_OBSERVER(m_soc_observer, APP_SOC_OBSERVER_PRIO, soc_evt_handler, NULL);


	//######### Other configuration

	//We also configure connection event length extension to increase the throughput
	ble_opt_t opt;
	CheckedMemset(&opt, 0x00, sizeof(opt));
	opt.common_opt.conn_evt_ext.enable = 1;

	err = sd_ble_opt_set(BLE_COMMON_OPT_CONN_EVT_EXT, &opt);
	if (err != 0) logt("ERROR", "Could not configure conn length extension %u", err);


#endif //NRF52

	//Enable DC/DC (needs external LC filter, cmp. nrf51 reference manual page 43)
	err = sd_power_dcdc_mode_set(Boardconfig->dcDcEnabled ? NRF_POWER_DCDC_ENABLE : NRF_POWER_DCDC_DISABLE);
	logt("ERROR", "sd_power_dcdc_mode_set %u", err);
	APP_ERROR_CHECK(err); //OK

	// Set power mode
	err = sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
	logt("ERROR", "sd_power_mode_set %u", err);
	APP_ERROR_CHECK(err); //OK

	err = (u32)FruityHal::RadioSetTxPower(Conf::defaultDBmTX, FruityHal::TxRole::SCAN_INIT, 0);
	APP_ERROR_CHECK(err); //OK

	return (ErrorType)err;
}

void FruityHal::BleStackDeinit()
{
#ifndef SIM_ENABLED
#ifdef NRF51
	sd_softdevice_disable();
#else
	nrf_sdh_disable_request();
#endif
#endif // SIM_ENABLED
}

#define __________________BLE_STACK_EVT_FETCHING____________________
// ############### BLE Stack and Event Handling ########################

/*
	EventHandling for NRF51:
		Events are fetched from the BLE Stack using polling and a sleep function that will block until
		another event is generated. This ensures that all event handling code is executed in order.
		Low latency functionality such as Timer and UART events are implemented interrupt based.

	EventHandling for NRF52:
		The SoftDevice Handler library is used to fetch events interrupt based.
		The SoftDevice Interrupt is defined as SD_EVT_IRQn, which is SWI2_IRQn and is set to IRQ_PRIO 7.
		All high level functionality must be called from IRQ PRIO 7 so that it cannot interrupt the other handlers.
		Events such as Timer, UART RX, SPI RX, etc,... can be handeled on IRQ PRIO 6 but should only perform very
		little processing (mostly buffering). They can then set the SWI2 pending using the SetPendingEventIRQ method.
		IRQ PRIO 6 tasks should not modify any data except their own variables.
		The main() thread can be used as well but care must be taken as this will be interrupted by IRQ PRIO 7.
 */

static FruityHal::SystemEvents nrfSystemEventToGeneric(u32 event)
{
	switch (event)
	{
		case NRF_EVT_HFCLKSTARTED:
			return FruityHal::SystemEvents::HIGH_FREQUENCY_CLOCK_STARTED;
		case NRF_EVT_POWER_FAILURE_WARNING:
			return FruityHal::SystemEvents::POWER_FAILURE_WARNING;
		case NRF_EVT_FLASH_OPERATION_SUCCESS:
			return FruityHal::SystemEvents::FLASH_OPERATION_SUCCESS;
		case NRF_EVT_FLASH_OPERATION_ERROR:
			return FruityHal::SystemEvents::FLASH_OPERATION_ERROR;
		case NRF_EVT_RADIO_BLOCKED:
			return FruityHal::SystemEvents::RADIO_BLOCKED;
		case NRF_EVT_RADIO_CANCELED:
			return FruityHal::SystemEvents::RADIO_CANCELED;
		case NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN:
			return FruityHal::SystemEvents::RADIO_SIGNAL_CALLBACK_INVALID_RETURN;
		case NRF_EVT_RADIO_SESSION_IDLE:
			return FruityHal::SystemEvents::RADIO_SESSION_IDLE;
		case NRF_EVT_RADIO_SESSION_CLOSED:
			return FruityHal::SystemEvents::RADIO_SESSION_CLOSED;
		case NRF_EVT_NUMBER_OF_EVTS:
			return FruityHal::SystemEvents::NUMBER_OF_EVTS;
		default:
			return FruityHal::SystemEvents::UNKOWN_EVENT;

	}
}

//Checks for high level application events generated e.g. by low level interrupt events
void ProcessAppEvents()
{
	for (u32 i = 0; i < GS->amountOfEventLooperHandlers; i++)
	{
		GS->eventLooperHandlers[i]();
	}

	//When using the watchdog with a timeout smaller than 60 seconds, we feed it in our event loop
#if IS_ACTIVE(WATCHDOG)
	if(FM_WATCHDOG_TIMEOUT < 32768UL * 60){
		FruityHal::FeedWatchdog();
	}
#endif

	//Check if there is input on uart
	GS->terminal.CheckAndProcessLine();

#if IS_ACTIVE(BUTTONS)
	//Handle waiting button event
	if(GS->button1HoldTimeDs != 0){
		u32 holdTimeDs = GS->button1HoldTimeDs;
		GS->button1HoldTimeDs = 0;

		GS->buttonEventHandler(0, holdTimeDs);
	}
#endif

	//Handle Timer event that was waiting
	if (GS->passsedTimeSinceLastTimerHandlerDs > 0)
	{
		u16 timerDs = GS->passsedTimeSinceLastTimerHandlerDs;

		//Dispatch timer to all other modules
		GS->timerEventHandler(timerDs);

		//FIXME: Should protect this with a semaphore
		//because the timerInterrupt works asynchronously
		GS->passsedTimeSinceLastTimerHandlerDs -= timerDs;
	}
}

#if defined(NRF51) || defined(SIM_ENABLED)
void FruityHal::EventLooper()
{
	//Check for waiting events from the application
	ProcessAppEvents();


	while (true)
	{
		//Fetch BLE events
		u16 eventSize = GlobalState::SIZE_OF_EVENT_BUFFER;
		u32 err = sd_ble_evt_get((u8*)GS->currentEventBuffer, &eventSize);

		//Handle ble event event
		if (err == NRF_SUCCESS)
		{
			FruityHal::DispatchBleEvents((void*)GS->currentEventBuffer);
		}
		//No more events available
		else if (err == NRF_ERROR_NOT_FOUND)
		{
			break;
		}
		else
		{
			APP_ERROR_CHECK(err); //LCOV_EXCL_LINE assertion
			break;
		}
	}

	// Pull event from soc
	while(true){
		uint32_t evt_id;
		u32 err = sd_evt_get(&evt_id);

		if (err == NRF_ERROR_NOT_FOUND){
			break;
		} else {
			GS->systemEventHandler(nrfSystemEventToGeneric(evt_id)); // Call handler
		}
	}

	u32 err = sd_app_evt_wait();
	APP_ERROR_CHECK(err); // OK
	err = sd_nvic_ClearPendingIRQ(SD_EVT_IRQn);
	APP_ERROR_CHECK(err);  // OK
}



#else

//This will get called for all events on the ble stack and also for all other interrupts
static void nrf_sdh_fruitymesh_evt_handler(void * p_context)
{
	ProcessAppEvents();
}

//This is called for all BLE related events
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
	FruityHal::DispatchBleEvents(p_ble_evt);
}

//This is called for all SoC related events
static void soc_evt_handler(uint32_t evt_id, void * p_context)
{
	GS->systemEventHandler(nrfSystemEventToGeneric(evt_id));
}

//Register an Event handler for all stack events
NRF_SDH_STACK_OBSERVER(m_nrf_sdh_fruitymesh_evt_handler, NRF_SDH_BLE_STACK_OBSERVER_PRIO) =
{
    .handler   = nrf_sdh_fruitymesh_evt_handler,
    .p_context = NULL,
};

//This is our main() after the stack was initialized and is called in a while loop
void FruityHal::EventLooper()
{
	FruityHal::VirtualComProcessEvents();

	u32 err = sd_app_evt_wait();
	APP_ERROR_CHECK(err); // OK
}

#endif

//Sets the SWI2 IRQ for events so that we can immediately process our main event handler
//This not used for NRF51
void FruityHal::SetPendingEventIRQ()
{
#ifndef SIM_ENABLED
	sd_nvic_SetPendingIRQ(SD_EVT_IRQn);
#endif
}

#define __________________EVENT_HANDLERS____________________
// ############### Methods to be called on events ########################

void FruityHal::DispatchBleEvents(void const * eventVirtualPointer)
{
	const ble_evt_t& bleEvent = *((ble_evt_t const *)eventVirtualPointer);
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	u16 eventId = bleEvent.header.evt_id;
	u32 err;
	if (eventId == BLE_GAP_EVT_ADV_REPORT || eventId == BLE_GAP_EVT_RSSI_CHANGED) {
		logt("EVENTS2", "BLE EVENT %s (%d)", FruityHal::getBleEventNameString(eventId), eventId);
	}
	else {
		logt("EVENTS", "BLE EVENT %s (%d)", FruityHal::getBleEventNameString(eventId), eventId);
	}
	
	//Calls the Db Discovery modules event handler
#ifdef NRF51
	ble_db_discovery_on_ble_evt(&halMemory->discoveredServices, &bleEvent);
#elif defined(NRF52)
	ble_db_discovery_on_ble_evt(&bleEvent, &halMemory->discoveredServices);
#endif

	switch (bleEvent.header.evt_id)
	{
	case BLE_GAP_EVT_RSSI_CHANGED:
		{
			GapRssiChangedEvent rce(&bleEvent);
			DispatchEvent(rce);
		}
		break;
	case BLE_GAP_EVT_ADV_REPORT:
		{
#if (SDK == 15)
			//In the later version of the SDK, we need to call sd_ble_gap_scan_start again with a nullpointer to continue to receive scan data
			if (bleEvent.evt.gap_evt.params.adv_report.type.status != BLE_GAP_ADV_DATA_STATUS_INCOMPLETE_MORE_DATA)
			{
				ble_data_t scan_data;
				scan_data.len = BLE_GAP_SCAN_BUFFER_MAX;
				scan_data.p_data = GS->scanBuffer;
				err = sd_ble_gap_scan_start(NULL, &scan_data);
				if ((err != NRF_ERROR_INVALID_STATE) &&
				    (err != NRF_ERROR_RESOURCES)) APP_ERROR_CHECK(err);
			}
#endif
			GapAdvertisementReportEvent are(&bleEvent);
			DispatchEvent(are);
		}
		break;
	case BLE_GAP_EVT_CONNECTED:
		{
			FruityHal::GapConnectedEvent ce(&bleEvent);
			DispatchEvent(ce);
		}
		break;
	case BLE_GAP_EVT_DISCONNECTED:
		{
			FruityHal::GapDisconnectedEvent de(&bleEvent);
			DispatchEvent(de);
		}
		break;
	case BLE_GAP_EVT_TIMEOUT:
		{
			FruityHal::GapTimeoutEvent gte(&bleEvent);
			DispatchEvent(gte);
		}
		break;
	case BLE_GAP_EVT_SEC_INFO_REQUEST:
		{
			FruityHal::GapSecurityInfoRequestEvent sire(&bleEvent);
			DispatchEvent(sire);
		}
		break;
	case BLE_GAP_EVT_CONN_SEC_UPDATE:
		{
			FruityHal::GapConnectionSecurityUpdateEvent csue(&bleEvent);
			DispatchEvent(csue);
		}
		break;
	case BLE_GATTC_EVT_WRITE_RSP:
		{
#ifdef SIM_ENABLED
			//if(cherrySimInstance->currentNode->id == 37 && bleEvent.evt.gattc_evt.conn_handle == 680) printf("%04u Q@NODE %u EVT_WRITE_RSP received" EOL, cherrySimInstance->globalBreakCounter++, cherrySimInstance->currentNode->id);
#endif
			FruityHal::GattcWriteResponseEvent wre(&bleEvent);
			DispatchEvent(wre);
		}
		break;
	case BLE_GATTC_EVT_TIMEOUT: //jstodo untested event
		{
			FruityHal::GattcTimeoutEvent gte(&bleEvent);
			DispatchEvent(gte);
		}
		break;
	case BLE_GATTS_EVT_WRITE:
		{
			FruityHal::GattsWriteEvent gwe(&bleEvent);
			DispatchEvent(gwe);
		}
		break;
	case BLE_GATTC_EVT_HVX:
		{
			FruityHal::GattcHandleValueEvent hve(&bleEvent);
			DispatchEvent(hve);
		}
		break;
#if defined(NRF51) || defined(SIM_ENABLED)
	case BLE_EVT_TX_COMPLETE:
#elif defined(NRF52)
	case BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE:
	case BLE_GATTS_EVT_HVN_TX_COMPLETE:
#endif
		{
#ifdef SIM_ENABLED
			//if (cherrySimInstance->currentNode->id == 37 && bleEvent.evt.common_evt.conn_handle == 680) printf("%04u Q@NODE %u WRITE_CMD_TX_COMPLETE %u received" EOL, cherrySimInstance->globalBreakCounter++, cherrySimInstance->currentNode->id, bleEvent.evt.common_evt.params.tx_complete.count);
#endif
			FruityHal::GattDataTransmittedEvent gdte(&bleEvent);
			DispatchEvent(gdte);
		}
		break;




		/* Extremly platform dependent events below! 
		   Because they are so platform dependent, 
		   there is no handler for them and we have
		   to deal with them here. */

#if defined(NRF52)
	case BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST:
	{
		//We automatically answer all data length update requests
		//The softdevice will chose the parameters so that our configured NRF_SDH_BLE_GATT_MAX_MTU_SIZE fits into a link layer packet
		sd_ble_gap_data_length_update(bleEvent.evt.gap_evt.conn_handle, nullptr, nullptr);
	}
	break;

	case BLE_GAP_EVT_DATA_LENGTH_UPDATE:
	{
		ble_gap_evt_data_length_update_t const * params = (ble_gap_evt_data_length_update_t const *) &bleEvent.evt.gap_evt.params.data_length_update;

		logt("FH", "DLE Result: rx %u/%u, tx %u/%u",
				params->effective_params.max_rx_octets,
				params->effective_params.max_rx_time_us,
				params->effective_params.max_tx_octets,
				params->effective_params.max_tx_time_us);

		// => We do not notify the application and can assume that it worked if the other device has enough resources
		//    If it does not work, this link will have a slightly reduced throughput, so this is monitored in another place
	}
	break;

	case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
	{
		//We answer all MTU update requests with our max mtu that was configured
		u32 err = sd_ble_gatts_exchange_mtu_reply(bleEvent.evt.gatts_evt.conn_handle, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);

		u16 partnerMtu = bleEvent.evt.gatts_evt.params.exchange_mtu_request.client_rx_mtu;
		u16 effectiveMtu = NRF_SDH_BLE_GATT_MAX_MTU_SIZE < partnerMtu ? NRF_SDH_BLE_GATT_MAX_MTU_SIZE : partnerMtu;

		logt("FH", "Reply MTU Exchange (%u) on conn %u with %u", err, bleEvent.evt.gatts_evt.conn_handle, effectiveMtu);

		ConnectionManager::getInstance().MtuUpdatedHandler(bleEvent.evt.gatts_evt.conn_handle, effectiveMtu);

		break;
	}

	case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:
	{
		u16 partnerMtu = bleEvent.evt.gattc_evt.params.exchange_mtu_rsp.server_rx_mtu;
		u16 effectiveMtu = NRF_SDH_BLE_GATT_MAX_MTU_SIZE < partnerMtu ? NRF_SDH_BLE_GATT_MAX_MTU_SIZE : partnerMtu;

		logt("FH", "MTU for hnd %u updated to %u", bleEvent.evt.gattc_evt.conn_handle, effectiveMtu);

		ConnectionManager::getInstance().MtuUpdatedHandler(bleEvent.evt.gattc_evt.conn_handle, effectiveMtu);
	}
	break;

#endif
	case BLE_GATTS_EVT_SYS_ATTR_MISSING:	//jstodo untested event
		{
			u32 err = 0;
			//Handles missing Attributes, don't know why it is needed
			err = sd_ble_gatts_sys_attr_set(bleEvent.evt.gatts_evt.conn_handle, nullptr, 0, 0);
			logt("ERROR", "SysAttr %u", err);
		}
		break;
#ifdef NRF52
	case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
		{
			//Required for some iOS devices. 
			ble_gap_phys_t phy;
			phy.rx_phys = BLE_GAP_PHY_1MBPS;
			phy.tx_phys = BLE_GAP_PHY_1MBPS;

			sd_ble_gap_phy_update(bleEvent.evt.gap_evt.conn_handle, &phy);
		}
		break;
#endif
	}


	if (eventId == BLE_GAP_EVT_ADV_REPORT || eventId == BLE_GAP_EVT_RSSI_CHANGED) {
		logt("EVENTS2", "End of event");
	}
	else {
		logt("EVENTS", "End of event");
	}
}

FruityHal::GapConnParamUpdateEvent::GapConnParamUpdateEvent(void const * _evt)
	:GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_CONN_PARAM_UPDATE)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::GapEvent::GapEvent(void const * _evt)
	: BleEvent(_evt)
{
}

u16 FruityHal::GapEvent::getConnectionHandle() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.conn_handle;
}

u16 FruityHal::GapConnParamUpdateEvent::getMaxConnectionInterval() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.conn_param_update.conn_params.max_conn_interval;
}

FruityHal::GapRssiChangedEvent::GapRssiChangedEvent(void const * _evt)
	:GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_RSSI_CHANGED)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

i8 FruityHal::GapRssiChangedEvent::getRssi() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.rssi_changed.rssi;
}

FruityHal::GapAdvertisementReportEvent::GapAdvertisementReportEvent(void const * _evt)
	:GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_ADV_REPORT)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

i8 FruityHal::GapAdvertisementReportEvent::getRssi() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.rssi;
}

const u8 * FruityHal::GapAdvertisementReportEvent::getData() const
{
#if (SDK == 15)
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.data.p_data;
#else
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.data;
#endif
}

u32 FruityHal::GapAdvertisementReportEvent::getDataLength() const
{
#if (SDK == 15)
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.data.len;
#else
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.dlen;
#endif
}

const u8 * FruityHal::GapAdvertisementReportEvent::getPeerAddr() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.peer_addr.addr;
}

FruityHal::BleGapAddrType FruityHal::GapAdvertisementReportEvent::getPeerAddrType() const
{
	return (BleGapAddrType)((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.peer_addr.addr_type;
}

bool FruityHal::GapAdvertisementReportEvent::isConnectable() const
{
#if (SDK == 15)
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.type.connectable == 0x01;
#else
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.adv_report.type == BLE_GAP_ADV_TYPE_ADV_IND;
#endif
}

FruityHal::BleEvent::BleEvent(void const *_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent != nullptr) {
		//This is thrown if two events are processed at the same time, which is illegal.
		SIMEXCEPTION(IllegalStateException); //LCOV_EXCL_LINE assertion
	}
	((NrfHalMemory*)GS->halMemory)->currentEvent = (ble_evt_t const *)_evt;
}

#ifdef SIM_ENABLED
FruityHal::BleEvent::~BleEvent()
{
	((NrfHalMemory*)GS->halMemory)->currentEvent = nullptr;
}
#endif

FruityHal::GapConnectedEvent::GapConnectedEvent(void const * _evt)
	:GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_CONNECTED)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::GapRole FruityHal::GapConnectedEvent::getRole() const
{
	return (GapRole)(((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.connected.role);
}

u8 FruityHal::GapConnectedEvent::getPeerAddrType() const
{
	return (((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.connected.peer_addr.addr_type);
}

u16 FruityHal::GapConnectedEvent::getMinConnectionInterval() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.connected.conn_params.min_conn_interval;
}

const u8 * FruityHal::GapConnectedEvent::getPeerAddr() const
{
	return (((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.connected.peer_addr.addr);
}

FruityHal::GapDisconnectedEvent::GapDisconnectedEvent(void const * _evt)
	: GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_DISCONNECTED)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::BleHciError FruityHal::GapDisconnectedEvent::getReason() const
{
	return (FruityHal::BleHciError)((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.disconnected.reason;
}

FruityHal::GapTimeoutEvent::GapTimeoutEvent(void const * _evt)
	: GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_TIMEOUT)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::GapTimeoutSource FruityHal::GapTimeoutEvent::getSource() const
{
#if defined(NRF51) || defined(SIM_ENABLED)
	switch (((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.timeout.src)
	{
	case BLE_GAP_TIMEOUT_SRC_ADVERTISING:
		return GapTimeoutSource::ADVERTISING;
	case BLE_GAP_TIMEOUT_SRC_SECURITY_REQUEST:
		return GapTimeoutSource::SECURITY_REQUEST;
	case BLE_GAP_TIMEOUT_SRC_SCAN:
		return GapTimeoutSource::SCAN;
	case BLE_GAP_TIMEOUT_SRC_CONN:
		return GapTimeoutSource::CONNECTION;
	default:
		return GapTimeoutSource::INVALID;
	}
#elif defined(NRF52)
	switch (((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.timeout.src)
	{
#if (SDK != 15)
	case BLE_GAP_TIMEOUT_SRC_ADVERTISING:
		return GapTimeoutSource::ADVERTISING;
#endif
	case BLE_GAP_TIMEOUT_SRC_SCAN:
		return GapTimeoutSource::SCAN;
	case BLE_GAP_TIMEOUT_SRC_CONN:
		return GapTimeoutSource::CONNECTION;
	case BLE_GAP_TIMEOUT_SRC_AUTH_PAYLOAD:
		return GapTimeoutSource::AUTH_PAYLOAD;
	default:
		return GapTimeoutSource::INVALID;
	}
#endif
}

FruityHal::GapSecurityInfoRequestEvent::GapSecurityInfoRequestEvent(void const * _evt)
	: GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_SEC_INFO_REQUEST)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::GapConnectionSecurityUpdateEvent::GapConnectionSecurityUpdateEvent(void const * _evt)
	: GapEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GAP_EVT_CONN_SEC_UPDATE)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

u8 FruityHal::GapConnectionSecurityUpdateEvent::getKeySize() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.conn_sec_update.conn_sec.encr_key_size;
}

FruityHal::SecurityLevel FruityHal::GapConnectionSecurityUpdateEvent::getSecurityLevel() const
{
	return (FruityHal::SecurityLevel)(((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.conn_sec_update.conn_sec.sec_mode.lv);
}

FruityHal::SecurityMode FruityHal::GapConnectionSecurityUpdateEvent::getSecurityMode() const
{
	return (FruityHal::SecurityMode)(((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gap_evt.params.conn_sec_update.conn_sec.sec_mode.sm);
}

FruityHal::GattcEvent::GattcEvent(void const * _evt)
	: BleEvent(_evt)
{
}

u16 FruityHal::GattcEvent::getConnectionHandle() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.conn_handle;
}

FruityHal::BleGattEror FruityHal::GattcEvent::getGattStatus() const
{
	return nrfErrToGenericGatt(((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.gatt_status);
}

FruityHal::GattcWriteResponseEvent::GattcWriteResponseEvent(void const * _evt)
	: GattcEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GATTC_EVT_WRITE_RSP)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::GattcTimeoutEvent::GattcTimeoutEvent(void const * _evt)
	: GattcEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GATTC_EVT_TIMEOUT)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

FruityHal::GattDataTransmittedEvent::GattDataTransmittedEvent(void const * _evt)
	:BleEvent(_evt)
{
#if defined(NRF51) || defined(SIM_ENABLED)
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_EVT_TX_COMPLETE)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
#endif
}

u16 FruityHal::GattDataTransmittedEvent::getConnectionHandle() const
{
#if defined(NRF51) || defined(SIM_ENABLED)
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.common_evt.conn_handle;
#elif defined(NRF52)
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id == BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE) {
		return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.conn_handle;
	}
	else if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id == BLE_GATTS_EVT_HVN_TX_COMPLETE) {
		return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.conn_handle;
	}
	SIMEXCEPTION(InvalidStateException);
	return -1; //This must never be executed!
#endif
}

bool FruityHal::GattDataTransmittedEvent::isConnectionHandleValid() const
{
	return getConnectionHandle() != BLE_CONN_HANDLE_INVALID;
}

u32 FruityHal::GattDataTransmittedEvent::getCompleteCount() const
{
#if defined(NRF51) || defined(SIM_ENABLED)
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.common_evt.params.tx_complete.count;
#elif defined(NRF52)
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id == BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE) {
		return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.params.write_cmd_tx_complete.count;
	}
	else if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id == BLE_GATTS_EVT_HVN_TX_COMPLETE) {
		return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.params.hvn_tx_complete.count;
	}
	SIMEXCEPTION(InvalidStateException);
	return -1; //This must never be executed!
#endif
}

FruityHal::GattsWriteEvent::GattsWriteEvent(void const * _evt)
	: BleEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GATTS_EVT_WRITE)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

u16 FruityHal::GattsWriteEvent::getAttributeHandle() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.params.write.handle;
}

bool FruityHal::GattsWriteEvent::isWriteRequest() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.params.write.op == BLE_GATTS_OP_WRITE_REQ;
}

u16 FruityHal::GattsWriteEvent::getLength() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.params.write.len;
}

u16 FruityHal::GattsWriteEvent::getConnectionHandle() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.conn_handle;
}

u8 const * FruityHal::GattsWriteEvent::getData() const
{
	return (u8 const *)((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gatts_evt.params.write.data;
}

FruityHal::GattcHandleValueEvent::GattcHandleValueEvent(void const * _evt)
	:GattcEvent(_evt)
{
	if (((NrfHalMemory*)GS->halMemory)->currentEvent->header.evt_id != BLE_GATTC_EVT_HVX)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}
}

u16 FruityHal::GattcHandleValueEvent::getHandle() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.params.hvx.handle;
}

u16 FruityHal::GattcHandleValueEvent::getLength() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.params.hvx.len;
}

u8 const * FruityHal::GattcHandleValueEvent::getData() const
{
	return ((NrfHalMemory*)GS->halMemory)->currentEvent->evt.gattc_evt.params.hvx.data;
}

/*
	#####################
	###               ###
	###      GAP      ###
	###               ###
	#####################
*/


#define __________________GAP____________________

u32 FruityHal::BleGapAddressSet(FruityHal::BleGapAddr const *address)
{
	ble_gap_addr_t addr;
	CheckedMemcpy(addr.addr, address->addr, FH_BLE_GAP_ADDR_LEN);
	addr.addr_type = (u8)address->addr_type;

#if defined(NRF51) || defined(SIM_ENABLED)
	u32 err = sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE, &addr);
#elif defined(NRF52)
	u32 err = sd_ble_gap_addr_set(&addr);
#endif
	logt("FH", "Gap Addr Set (%u)", err);

	return err;
}

u32 FruityHal::BleGapAddressGet(FruityHal::BleGapAddr *address)
{
	u32 err;
	ble_gap_addr_t p_addr;

#if defined(NRF51) || defined(SIM_ENABLED)
	err = sd_ble_gap_address_get(&p_addr);
#elif defined(NRF52)
	err = sd_ble_gap_addr_get(&p_addr);
#endif

	if(err == NRF_SUCCESS){
		CheckedMemcpy(address->addr, p_addr.addr, FH_BLE_GAP_ADDR_LEN);
		address->addr_type = (BleGapAddrType)p_addr.addr_type;
	}

	logt("FH", "Gap Addr Get (%u)", err);

	return err;
}

ErrorType FruityHal::BleGapScanStart(BleGapScanParams const *scanParams)
{
	u32 err;
	ble_gap_scan_params_t scan_params;
	CheckedMemset(&scan_params, 0x00, sizeof(ble_gap_scan_params_t));

	scan_params.active = 0;
	scan_params.interval = scanParams->interval;
	scan_params.timeout = scanParams->timeout;
	scan_params.window = scanParams->window;

#if (SDK == 15)	
	scan_params.report_incomplete_evts = 0;
	scan_params.filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL;
	scan_params.extended = 0;
	scan_params.scan_phys = BLE_GAP_PHY_1MBPS;
	scan_params.timeout = scanParams->timeout * 100; //scanTimeout is now in ms since SDK15 instead of seconds
	CheckedMemset(scan_params.channel_mask, 0, sizeof(ble_gap_ch_mask_t));
#else
#if defined(NRF51) || defined(SIM_ENABLED)
	scan_params.p_whitelist = nullptr;
	scan_params.selective = 0;
#elif defined(NRF52)
	scan_params.adv_dir_report = 0;
	scan_params.use_whitelist = 0;
#endif
#endif

#if (SDK == 15)
	ble_data_t scan_data;
	scan_data.len = BLE_GAP_SCAN_BUFFER_MAX;
	scan_data.p_data = GS->scanBuffer;
	err = sd_ble_gap_scan_start(&scan_params, &scan_data);
#else
	err = sd_ble_gap_scan_start(&scan_params);
#endif
	logt("FH", "Scan start(%u) iv %u, w %u, t %u", err, scan_params.interval, scan_params.window, scan_params.timeout);
	return nrfErrToGeneric(err);
}


ErrorType FruityHal::BleGapScanStop()
{
	u32 err = sd_ble_gap_scan_stop();
	logt("FH", "Scan stop(%u)", err);
	return nrfErrToGeneric(err);
}

ErrorType FruityHal::BleGapAdvStart(u8 advHandle, BleGapAdvParams const *advParams)
{
	u32 err;
#if (SDK == 15)
	err = sd_ble_gap_adv_start(advHandle, BLE_CONN_CFG_TAG_FM);
	logt("FH", "Adv start (%u)", err);
#else      
	ble_gap_adv_params_t adv_params;
	adv_params.channel_mask.ch_37_off = advParams->channelMask.ch37Off;
	adv_params.channel_mask.ch_38_off = advParams->channelMask.ch38Off;
	adv_params.channel_mask.ch_39_off= advParams->channelMask.ch39Off;
	adv_params.fp = BLE_GAP_ADV_FP_ANY;
	adv_params.interval = advParams->interval;
	adv_params.p_peer_addr = nullptr;
	adv_params.timeout = advParams->timeout;
	adv_params.type = (u8)advParams->type;
#if defined(NRF51) || defined(SIM_ENABLED)
	err = sd_ble_gap_adv_start(&adv_params);
#elif defined(NRF52)
	err = sd_ble_gap_adv_start(&adv_params, BLE_CONN_CFG_TAG_FM);
#endif
	logt("FH", "Adv start (%u) typ %u, iv %u, mask %u", err, adv_params.type, adv_params.interval, *((u8*)&adv_params.channel_mask));
#endif
	return nrfErrToGeneric(err);
}

ErrorType FruityHal::BleGapAdvDataSet(u8 * p_advHandle, BleGapAdvParams const * p_advParams, u8 *advData, u8 advDataLength, u8 *scanData, u8 scanDataLength)
{
	u32 err = 0;
#if (SDK == 15)
	ble_gap_adv_params_t adv_params;
	ble_gap_adv_data_t adv_data;
	CheckedMemset(&adv_params, 0, sizeof(adv_params));
	CheckedMemset(&adv_data, 0, sizeof(adv_data));
	if (p_advParams != nullptr)	{
		adv_params.channel_mask[4] |= (p_advParams->channelMask.ch37Off << 5);
		adv_params.channel_mask[4] |= (p_advParams->channelMask.ch38Off << 6);
		adv_params.channel_mask[4] |= (p_advParams->channelMask.ch39Off << 7);
		adv_params.filter_policy = BLE_GAP_ADV_FP_ANY;
		adv_params.interval = p_advParams->interval;
		adv_params.p_peer_addr = nullptr;
		adv_params.duration = p_advParams->timeout;
		adv_params.properties.type = (u8)p_advParams->type;
	}
	adv_data.adv_data.p_data = (u8 *)advData;
	adv_data.adv_data.len = advDataLength;
	adv_data.scan_rsp_data.p_data = (u8 *)scanData;
	adv_data.scan_rsp_data.len = scanDataLength;

	if (p_advParams != nullptr){
		err = sd_ble_gap_adv_set_configure(
					p_advHandle,
					&adv_data,
				  &adv_params
				);
	}
	else {
		err = sd_ble_gap_adv_set_configure(
					p_advHandle,
					&adv_data,
				  nullptr
				);
	}

	logt("FH", "Adv data set (%u) typ %u, iv %lu, mask %u, handle %u", err, adv_params.properties.type, adv_params.interval, adv_params.channel_mask[4], *p_advHandle);
#else
	err = sd_ble_gap_adv_data_set(
				advData,
				advDataLength,
				scanData,
				scanDataLength
			);

	logt("FH", "Adv data set (%u)", err);
#endif
	return nrfErrToGeneric(err);
}

ErrorType FruityHal::BleGapAdvStop(u8 advHandle)
{
	u32 err;
#if (SDK == 15)
	err = sd_ble_gap_adv_stop(advHandle);
#else
	err = sd_ble_gap_adv_stop();
#endif
	logt("FH", "Adv stop (%u)", err);
	return nrfErrToGeneric(err);
}

ErrorType FruityHal::BleGapConnect(FruityHal::BleGapAddr const *peerAddress, BleGapScanParams const *scanParams, BleGapConnParams const *connectionParams)
{
	u32 err;
	ble_gap_addr_t p_peer_addr;
	CheckedMemset(&p_peer_addr, 0x00, sizeof(p_peer_addr));
	p_peer_addr.addr_type = (u8)peerAddress->addr_type;
	CheckedMemcpy(p_peer_addr.addr, peerAddress->addr, sizeof(peerAddress->addr));

	ble_gap_scan_params_t p_scan_params;
	CheckedMemset(&p_scan_params, 0x00, sizeof(p_scan_params));
	p_scan_params.active = 0;
	p_scan_params.interval = scanParams->interval;
	p_scan_params.timeout = scanParams->timeout;
	p_scan_params.window = scanParams->window;
#if defined(NRF51) || defined(SIM_ENABLED)
	p_scan_params.p_whitelist = nullptr;
	p_scan_params.selective = 0;
#elif defined(NRF52) && (SDK == 14)
	p_scan_params.adv_dir_report = 0;
	p_scan_params.use_whitelist = 0;
#else
	p_scan_params.report_incomplete_evts = 0;
	p_scan_params.filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL;
	p_scan_params.extended = 0;
	CheckedMemset(p_scan_params.channel_mask, 0, sizeof(p_scan_params.channel_mask));
#endif

	ble_gap_conn_params_t p_conn_params;
	CheckedMemset(&p_conn_params, 0x00, sizeof(p_conn_params));
	p_conn_params.conn_sup_timeout = connectionParams->connSupTimeout;
	p_conn_params.max_conn_interval = connectionParams->maxConnInterval;
	p_conn_params.min_conn_interval = connectionParams->minConnInterval;
	p_conn_params.slave_latency = connectionParams->slaveLatency;


#if defined(NRF51) || defined(SIM_ENABLED)
	err = sd_ble_gap_connect(&p_peer_addr, &p_scan_params, &p_conn_params);
#elif defined(NRF52)
	err = sd_ble_gap_connect(&p_peer_addr, &p_scan_params, &p_conn_params, BLE_CONN_CFG_TAG_FM);
#endif

	logt("FH", "Connect (%u) iv:%u, tmt:%u", err, p_conn_params.min_conn_interval, p_scan_params.timeout);

	//Tell our ScanController, that scanning has stopped
	GS->scanController.ScanningHasStopped();

	return nrfErrToGeneric(err);
}


u32 FruityHal::ConnectCancel()
{
	u32 err = sd_ble_gap_connect_cancel();

	logt("FH", "Connect Cancel (%u)", err);

	return err;
}

ErrorType FruityHal::Disconnect(u16 conn_handle, FruityHal::BleHciError hci_status_code)
{
	u32 err = sd_ble_gap_disconnect(conn_handle, (u8)hci_status_code);

	logt("FH", "Disconnect (%u)", err);

	return nrfErrToGeneric(err);
}

ErrorType FruityHal::BleTxPacketCountGet(u16 connectionHandle, u8* count)
{
#if defined(NRF51) || defined(SIM_ENABLED)
	return nrfErrToGeneric(sd_ble_tx_packet_count_get(connectionHandle, count));
#elif defined(NRF52)
//TODO: must be read from somewhere else
	*count = BLE_CONN_CFG_GAP_PACKET_BUFFERS;
	return ErrorType::SUCCESS;
#endif
}

u32 FruityHal::BleGapNameSet(BleGapConnSecMode & mode, u8 const * p_dev_name, u16 len)
{
	ble_gap_conn_sec_mode_t sec_mode;
	CheckedMemset(&sec_mode, 0, sizeof(sec_mode));
	sec_mode.sm = mode.securityMode;
	sec_mode.lv = mode.level;
	return sd_ble_gap_device_name_set(&sec_mode, p_dev_name, len);
}

u32 FruityHal::BleGapAppearance(BleAppearance appearance)
{
	return sd_ble_gap_appearance_set((u32)appearance);
}

ble_gap_conn_params_t translate(FruityHal::BleGapConnParams const & params)
{
	ble_gap_conn_params_t gapConnectionParams;
	CheckedMemset(&gapConnectionParams, 0, sizeof(gapConnectionParams));

	gapConnectionParams.min_conn_interval = params.minConnInterval;
	gapConnectionParams.max_conn_interval = params.maxConnInterval;
	gapConnectionParams.slave_latency = params.slaveLatency;
	gapConnectionParams.conn_sup_timeout = params.connSupTimeout;
	return gapConnectionParams;
}

ErrorType FruityHal::BleGapConnectionParamsUpdate(u16 conn_handle, BleGapConnParams const & params)
{
	ble_gap_conn_params_t gapConnectionParams = translate(params);
	return nrfErrToGeneric(sd_ble_gap_conn_param_update(conn_handle, &gapConnectionParams));
}

u32 FruityHal::BleGapConnectionPreferredParamsSet(BleGapConnParams const & params)
{
	ble_gap_conn_params_t gapConnectionParams = translate(params);
	return sd_ble_gap_ppcp_set(&gapConnectionParams);
}

u32 FruityHal::BleGapSecInfoReply(u16 conn_handle, BleGapEncInfo * p_info, u8 * p_id_info, u8 * p_sign_info)
{
	ble_gap_enc_info_t info;
	CheckedMemset(&info, 0, sizeof(info));
	CheckedMemcpy(info.ltk, p_info->longTermKey, p_info->longTermKeyLength);
	info.lesc = p_info->isGeneratedUsingLeSecureConnections ;
	info.auth = p_info->isAuthenticatedKey;
	info.ltk_len = p_info->longTermKeyLength;

	return sd_ble_gap_sec_info_reply(
		conn_handle,
		&info, //This is our stored long term key
		nullptr, //We do not have an identity resolving key
		nullptr //We do not have signing info
	);
}

u32 FruityHal::BleGapEncrypt(u16 conn_handle, BleGapMasterId const & master_id, BleGapEncInfo const & enc_info)
{
	ble_gap_master_id_t keyId;
	CheckedMemset(&keyId, 0, sizeof(keyId));
	keyId.ediv = master_id.encryptionDiversifier;
	CheckedMemcpy(keyId.rand, master_id.rand, BLE_GAP_SEC_RAND_LEN);

	ble_gap_enc_info_t info;
	CheckedMemset(&info, 0, sizeof(info));
	CheckedMemcpy(info.ltk, enc_info.longTermKey, enc_info.longTermKeyLength);
	info.lesc = enc_info.isGeneratedUsingLeSecureConnections ;
	info.auth = enc_info.isAuthenticatedKey;
	info.ltk_len = enc_info.longTermKeyLength;

	return sd_ble_gap_encrypt(conn_handle, &keyId, &info);
}

u32 FruityHal::BleGapRssiStart(u16 conn_handle, u8 threshold_dbm, u8 skip_count)
{
	return sd_ble_gap_rssi_start(conn_handle, threshold_dbm, skip_count);
}

u32 FruityHal::BleGapRssiStop(u16 conn_handle)
{
	return sd_ble_gap_rssi_stop(conn_handle);
}



/*
	#####################
	###               ###
	###      GATT     ###
	###               ###
	#####################
*/


#define __________________GATT____________________


#ifndef SIM_ENABLED
FruityHal::DBDiscoveryHandler dbDiscoveryHandler = nullptr;

static void DatabaseDiscoveryHandler(ble_db_discovery_evt_t * p_evt)
{
	// other events are not supported
	if (!(p_evt->evt_type == BLE_DB_DISCOVERY_SRV_NOT_FOUND ||
	      p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE)) return;

	FruityHal::BleGattDBDiscoveryEvent bleDbEvent;
	CheckedMemset(&bleDbEvent, 0x00, sizeof(bleDbEvent));
	bleDbEvent.connHandle = p_evt->conn_handle;
	bleDbEvent.type = p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE ? FruityHal::BleGattDBDiscoveryEventType::COMPLETE : FruityHal::BleGattDBDiscoveryEventType::SERVICE_NOT_FOUND;
	bleDbEvent.serviceUUID.uuid = p_evt->params.discovered_db.srv_uuid.uuid;
	bleDbEvent.serviceUUID.type = p_evt->params.discovered_db.srv_uuid.type;
	bleDbEvent.charateristicsCount = p_evt->params.discovered_db.char_count;
	for (u8 i = 0; i < bleDbEvent.charateristicsCount; i++)
	{
	  bleDbEvent.dbChar[i].handleValue = p_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
	  bleDbEvent.dbChar[i].charUUID.uuid = p_evt->params.discovered_db.charateristics[i].characteristic.uuid.uuid;
	  bleDbEvent.dbChar[i].charUUID.type = p_evt->params.discovered_db.charateristics[i].characteristic.uuid.type;
	  bleDbEvent.dbChar[i].cccdHandle = p_evt->params.discovered_db.charateristics[i].cccd_handle;
	}
	logt("ERROR", "########################BLE DB EVENT");
	dbDiscoveryHandler(&bleDbEvent);
}
#endif //SIM_ENABLED

u32 FruityHal::DiscovereServiceInit(DBDiscoveryHandler dbEventHandler)
{
#ifndef  SIM_ENABLED
	dbDiscoveryHandler = dbEventHandler;
	return ble_db_discovery_init(DatabaseDiscoveryHandler);
#else
	GS->dbDiscoveryHandler = dbEventHandler;
#endif
	return 0;
}

u32 FruityHal::DiscoverService(u16 connHandle, const BleGattUuid &p_uuid)
{
	uint32_t err = 0;
	ble_uuid_t uuid;
	CheckedMemset(&uuid, 0, sizeof(uuid));
	uuid.uuid = p_uuid.uuid;
	uuid.type = p_uuid.type;
#ifndef SIM_ENABLED
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	CheckedMemset(&halMemory->discoveredServices, 0x00, sizeof(halMemory->discoveredServices));
	err = ble_db_discovery_evt_register(&uuid);
	if (err) {
		logt("ERROR", "err %u", (u32)err);
		return err;
	}

	err = ble_db_discovery_start(&halMemory->discoveredServices, connHandle);
	if (err) {
		logt("ERROR", "err %u", (u32)err);
		return err;
	}
#else
	cherrySimInstance->StartServiceDiscovery(connHandle, uuid, 1000);
#endif
	return err;
}

bool FruityHal::DiscoveryIsInProgress()
{
#ifndef SIM_ENABLED
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	return halMemory->discoveredServices.discovery_in_progress;
#else
	return false;
#endif
}

u32 FruityHal::BleGattSendNotification(u16 connHandle, BleGattWriteParams & params)
{	
	ble_gatts_hvx_params_t notificationParams;
	CheckedMemset(&notificationParams, 0, sizeof(ble_gatts_hvx_params_t));
	notificationParams.handle = params.handle;
	notificationParams.offset = params.offset;
	notificationParams.p_data = params.p_data;
	notificationParams.p_len = &params.len;

	if (params.type == BleGattWriteType::NOTIFICATION) notificationParams.type = BLE_GATT_HVX_NOTIFICATION;
	else if (params.type == BleGattWriteType::INDICATION) notificationParams.type = BLE_GATT_HVX_INDICATION;
	else return NRF_ERROR_INVALID_PARAM;
	
	return sd_ble_gatts_hvx(connHandle, &notificationParams);
}

u32 FruityHal::BleGattWrite(u16 connHandle, BleGattWriteParams const & params)
{
	ble_gattc_write_params_t writeParameters;
	CheckedMemset(&writeParameters, 0, sizeof(ble_gattc_write_params_t));
	writeParameters.handle = params.handle;
	writeParameters.offset = params.offset;
	writeParameters.len = params.len;
	writeParameters.p_value = params.p_data;

	if (params.type == BleGattWriteType::WRITE_REQ) writeParameters.write_op = BLE_GATT_OP_WRITE_REQ;
	else if (params.type == BleGattWriteType::WRITE_CMD) writeParameters.write_op = BLE_GATT_OP_WRITE_CMD;
	else return NRF_ERROR_INVALID_PARAM;

	return sd_ble_gattc_write(connHandle, &writeParameters);	
}

u32 FruityHal::BleUuidVsAdd(u8 const * p_vs_uuid, u8 * p_uuid_type)
{
	ble_uuid128_t vs_uuid;
	CheckedMemset(&vs_uuid, 0, sizeof(vs_uuid));
	CheckedMemcpy(vs_uuid.uuid128, p_vs_uuid, sizeof(vs_uuid));
	return sd_ble_uuid_vs_add(&vs_uuid, p_uuid_type);
}

u32 FruityHal::BleGattServiceAdd(BleGattSrvcType type, BleGattUuid const & p_uuid, u16 * p_handle)
{
	ble_uuid_t uuid;
	CheckedMemset(&uuid, 0, sizeof(uuid));
	uuid.uuid = p_uuid.uuid;
	uuid.type = p_uuid.type;
	return sd_ble_gatts_service_add((u8)type, &uuid, p_handle);
}

u32 FruityHal::BleGattCharAdd(u16 service_handle, BleGattCharMd const & char_md, BleGattAttribute const & attr_char_value, BleGattCharHandles & handles)
{
	ble_gatts_char_md_t sd_char_md;
	ble_gatts_attr_t sd_attr_char_value;
	
	static_assert(SDK <= 15, "Check mapping");

	CheckedMemcpy(&sd_char_md, &char_md, sizeof(ble_gatts_char_md_t));
	CheckedMemcpy(&sd_attr_char_value, &attr_char_value, sizeof(ble_gatts_attr_t));
	return sd_ble_gatts_characteristic_add(service_handle, &sd_char_md, &sd_attr_char_value, (ble_gatts_char_handles_t *)&handles);
}

u32 FruityHal::BleGapDataLengthExtensionRequest(u16 connHandle)
{
#ifdef NRF52
	//We let the SoftDevice decide the maximum according to the NRF_SDH_BLE_GATT_MAX_MTU_SIZE and connection configuration
	u32 err = sd_ble_gap_data_length_update(connHandle, nullptr, nullptr);
	logt("FH", "Start DLE Update (%u) on conn %u", err, connHandle);

	return err;
#else
	//TODO: We should implement DLE in the Simulator as soon as it is based on the NRF52

	return NRF_ERROR_NOT_SUPPORTED;
#endif
}

u32 FruityHal::BleGattGetMaxMtu()
{
#ifdef SIM_ENABLED
	return 63;
#elif NRF51
	return 23; //NRF51 does not support a higher MTU
#else
	return NRF_SDH_BLE_GATT_MAX_MTU_SIZE; //MTU for nRF52 is defined through sdk_config.h
#endif
}

u32 FruityHal::BleGattMtuExchangeRequest(u16 connHandle, u16 clientRxMtu)
{
#ifdef NRF52
	u32 err = sd_ble_gattc_exchange_mtu_request(connHandle, clientRxMtu);

	logt("FH", "Start MTU Exchange (%u) on conn %u with %u", err, connHandle, clientRxMtu);

	return err;
#else
	//TODO: We should implement MTU Exchange in the Simulator as soon as it is based on the NRF52

	return NRF_ERROR_NOT_SUPPORTED;
#endif
}

u32 FruityHal::BleGattMtuExchangeReply(u16 connHandle, u16 clientRxMtu)
{
#ifdef NRF52
	u32 err = sd_ble_gatts_exchange_mtu_reply(connHandle, clientRxMtu);

	logt("ERROR", "Reply MTU Exchange (%u) on conn %u with %u", err, connHandle, clientRxMtu);

	return err;
#else
	return NRF_ERROR_NOT_SUPPORTED;
#endif
}

// Supported tx_power values for this implementation: -40dBm, -20dBm, -16dBm, -12dBm, -8dBm, -4dBm, 0dBm, +3dBm and +4dBm.
ErrorType FruityHal::RadioSetTxPower(i8 tx_power, TxRole role, u16 handle)
{
	if (tx_power != -40 && 
		  tx_power != -30 && 
		  tx_power != -20 && 
		  tx_power != -16 && 
		  tx_power != -12 && 
		  tx_power != -8  && 
		  tx_power != -4  && 
		  tx_power != 0   && 
		  tx_power != 4) {
		SIMEXCEPTION(IllegalArgumentException);
		return ErrorType::INVALID_PARAM;
	}

	u32 err;
#if (SDK == 15)
	u8 txRole;
	if (role == TxRole::CONNECTION) txRole = BLE_GAP_TX_POWER_ROLE_CONN;
	else if (role == TxRole::ADVERTISING) txRole = BLE_GAP_TX_POWER_ROLE_ADV;
	else if (role == TxRole::SCAN_INIT) txRole = BLE_GAP_TX_POWER_ROLE_SCAN_INIT;
	else return ErrorType::INVALID_PARAM;;
	err = sd_ble_gap_tx_power_set(txRole, handle, tx_power);
#else
	err = sd_ble_gap_tx_power_set(tx_power);
#endif

	return nrfErrToGeneric(err);
}


//################################################
#define _________________BUTTONS__________________

#ifndef SIM_ENABLED
void button_interrupt_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action){
	//GS->ledGreen->Toggle();

	//Because we don't know which state the button is in, we have to read it
	u32 state = nrf_gpio_pin_read(pin);

	//An interrupt generated by our button
	if(pin == (u8)Boardconfig->button1Pin){
		if(state == Boardconfig->buttonsActiveHigh){
			GS->button1PressTimeDs = GS->appTimerDs;
		} else if(state == !Boardconfig->buttonsActiveHigh && GS->button1PressTimeDs != 0){
			GS->button1HoldTimeDs = GS->appTimerDs - GS->button1PressTimeDs;
			GS->button1PressTimeDs = 0;
		}
	}
}
#endif

ErrorType FruityHal::WaitForEvent()
{
	return nrfErrToGeneric(sd_app_evt_wait());
}

u32 FruityHal::InitializeButtons()
{
	u32 err = 0;
#if IS_ACTIVE(BUTTONS) && !defined(SIM_ENABLED)
	//Activate GPIOTE if not already active
	nrf_drv_gpiote_init();

	//Register for both HighLow and LowHigh events
	//IF this returns NO_MEM, increase GPIOTE_CONFIG_NUM_OF_LOW_POWER_EVENTS
	nrf_drv_gpiote_in_config_t buttonConfig;
	buttonConfig.sense = NRF_GPIOTE_POLARITY_TOGGLE;
	buttonConfig.pull = NRF_GPIO_PIN_PULLUP;
	buttonConfig.is_watcher = 0;
	buttonConfig.hi_accuracy = 0;
#if SDK == 15
	buttonConfig.skip_gpio_setup = 0;
#endif

	//This uses the SENSE low power feature, all pin events are reported
	//at the same GPIOTE channel
	err =  nrf_drv_gpiote_in_init(Boardconfig->button1Pin, &buttonConfig, button_interrupt_handler);

	//Enable the events
	nrf_drv_gpiote_in_event_enable(Boardconfig->button1Pin, true);
#endif

	return err;
}

ErrorType FruityHal::GetRandomBytes(u8 * p_data, u8 len)
{
	return nrfErrToGeneric(sd_rand_application_vector_get(p_data, len));
}

//################################################
#define _________________UART_____________________

//This handler receives UART interrupts (terminal json mode)
#if !defined(UART_ENABLED) || UART_ENABLED == 0 //Only enable if nordic library for UART is not used
extern "C"{
	void UART0_IRQHandler(void)
	{
		if (GS->uartEventHandler == nullptr) {
			SIMEXCEPTION(UartNotSetException);
		} else {
		    GS->uartEventHandler();
		}
	}
}
#endif



//################################################
#define _________________VIRTUAL_COM_PORT_____________________

void FruityHal::VirtualComInitBeforeStack()
{
#if IS_ACTIVE(VIRTUAL_COM_PORT)
	virtualComInit();
#endif
}
void FruityHal::VirtualComInitAfterStack(void (*portEventHandler)(bool))
{
#if IS_ACTIVE(VIRTUAL_COM_PORT)
	virtualComStart(portEventHandler);
#endif
}
void FruityHal::VirtualComProcessEvents()
{
#if IS_ACTIVE(VIRTUAL_COM_PORT)
	virtualComCheck();
#endif
}
u32 FruityHal::VirtualComCheckAndProcessLine(u8* buffer, u16 bufferLength)
{
#if IS_ACTIVE(VIRTUAL_COM_PORT)
	return virtualComCheckAndProcessLine(buffer, bufferLength);
#else
	return 0;
#endif
}
void FruityHal::VirtualComWriteData(const u8* data, u16 dataLength)
{
#if IS_ACTIVE(VIRTUAL_COM_PORT)
	virtualComWriteData(data, dataLength);
#endif
}

//################################################
#define _________________TIMERS___________________

extern "C"{
	static const u32 TICKS_PER_DS_TIMES_TEN = 32768;

	void app_timer_handler(void * p_context){
		UNUSED_PARAMETER(p_context);

		//We just increase the time that has passed since the last handler
		//And call the timer from our main event handling queue
		GS->tickRemainderTimesTen += ((u32)MAIN_TIMER_TICK) * 10;
		u32 passedDs = GS->tickRemainderTimesTen / TICKS_PER_DS_TIMES_TEN;
		GS->tickRemainderTimesTen -= passedDs * TICKS_PER_DS_TIMES_TEN;
		GS->passsedTimeSinceLastTimerHandlerDs += passedDs;

		FruityHal::SetPendingEventIRQ();

		GS->timeManager.AddTicks(MAIN_TIMER_TICK);

	}
}

u32 FruityHal::InitTimers()
{
	SIMEXCEPTION(NotImplementedException);
#if defined(NRF51)
	APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, nullptr);
#elif defined(NRF52)
	uint32_t ret = app_timer_init();
	return ret;
#endif
	return 0;
}

u32 FruityHal::StartTimers()
{
	SIMEXCEPTION(NotImplementedException);
	u32 err = 0;
#ifndef SIM_ENABLED

	APP_TIMER_DEF(mainTimerMsId);

	err = app_timer_create(&mainTimerMsId, APP_TIMER_MODE_REPEATED, app_timer_handler);
	if (err != NRF_SUCCESS) return err;

	err = app_timer_start(mainTimerMsId, MAIN_TIMER_TICK, nullptr);
#endif // SIM_ENABLED
	return err;
}

ErrorType FruityHal::CreateTimer(FruityHal::swTimer &timer, bool repeated, TimerHandler handler)
{	
	SIMEXCEPTION(NotImplementedException);
	u32 err;
#ifndef SIM_ENABLED

	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	timer = (u32 *)&halMemory->swTimers[halMemory->timersCreated];
	CheckedMemset(timer, 0x00, sizeof(app_timer_t));

	app_timer_mode_t mode = repeated ? APP_TIMER_MODE_REPEATED : APP_TIMER_MODE_SINGLE_SHOT;
	
	err = app_timer_create((app_timer_id_t *)(&timer), mode, handler);
	if (err != NRF_SUCCESS) return nrfErrToGeneric(err);

	halMemory->timersCreated++;
#endif
	return ErrorType::SUCCESS;
}

ErrorType FruityHal::StartTimer(FruityHal::swTimer timer, u32 timeoutMs)
{
	SIMEXCEPTION(NotImplementedException);
#ifndef SIM_ENABLED
	if (timer == nullptr) return ErrorType::INVALID_PARAM;

#ifdef NRF51
	u32 err = app_timer_start((app_timer_id_t)timer, APP_TIMER_TICKS(timeoutMs, APP_TIMER_PRESCALER), NULL);
#else
	// APP_TIMER_TICKS number of parameters has changed from SDK 11 to SDK 14
	// cppcheck-suppress preprocessorErrorDirective
	u32 err = app_timer_start((app_timer_id_t)timer, APP_TIMER_TICKS(timeoutMs), NULL);
#endif
	return nrfErrToGeneric(err);
#else
	return ErrorType::SUCCESS;
#endif
}

ErrorType FruityHal::StopTimer(FruityHal::swTimer timer)
{
	SIMEXCEPTION(NotImplementedException);
#ifndef SIM_ENABLED
	if (timer == nullptr) return ErrorType::INVALID_PARAM;

	u32 err = app_timer_stop((app_timer_id_t)timer);
	return nrfErrToGeneric(err);
#else
	return ErrorType::SUCCESS;
#endif
}

u32 FruityHal::GetRtcMs()
{
	uint32_t rtcTicks;
#if defined(NRF51) || defined(SIM_ENABLED)
	app_timer_cnt_get(&rtcTicks);
#elif defined(NRF52)
	rtcTicks = app_timer_cnt_get();
#endif
	return rtcTicks * 1000 / APP_TIMER_CLOCK_FREQ;
}

// In this port limitation is that max diff between nowTimeMs and previousTimeMs
// can be 0xFFFFFE measured in app_timer ticks. If the difference is bigger the
// result will be faulty
u32 FruityHal::GetRtcDifferenceMs(u32 nowTimeMs, u32 previousTimeMs)
{
	u32 nowTimeTicks = nowTimeMs * APP_TIMER_CLOCK_FREQ / 1000;
	u32 previousTimeTicks = previousTimeMs * APP_TIMER_CLOCK_FREQ / 1000;
	uint32_t diffTicks;
#if defined(NRF51) || defined(SIM_ENABLED)
	app_timer_cnt_diff_compute(nowTimeTicks, previousTimeTicks, &diffTicks);
#elif defined(NRF52)
	diffTicks = app_timer_cnt_diff_compute(nowTimeTicks, previousTimeTicks);
#endif
	return diffTicks * 1000 / APP_TIMER_CLOCK_FREQ;
}

//################################################
#define _____________FAULT_HANDLERS_______________

//These error handlers are declared WEAK in the nRF SDK and can be implemented here
//Will be called if an error occurs somewhere in the code, but not if it's a hardfault
extern "C"
{
	//The app_error handler_bare is called by all APP_ERROR_CHECK functions when DEBUG is undefined
	void app_error_handler_bare(uint32_t error_code)
	{
		GS->appErrorHandler((u32)error_code);
	}

	//The app_error handler is called by all APP_ERROR_CHECK functions when DEBUG is defined
	void app_error_handler(uint32_t error_code, uint32_t line_num, const u8 * p_file_name)
	{
		app_error_handler_bare(error_code);
		logt("ERROR", "App error code:%s(%u), file:%s, line:%u", Logger::getGeneralErrorString((ErrorType)error_code), (u32)error_code, p_file_name, (u32)line_num);
	}

	//Called when the softdevice crashes
	void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
	{
		GS->stackErrorHandler(id, pc, info);
	}

#ifndef SIM_ENABLED
	//We use the nordic hardfault handler that stacks all fault variables for us before calling this function
	__attribute__((used)) void HardFault_c_handler(stacked_regs_t* stack)
	{
		GS->hardfaultHandler(stack);
	}
#endif

	//NRF52 uses more handlers, we currently just reboot if they are called
	//TODO: Redirect to hardfault handler, but be aware that the stack will shift by calling the function
#ifdef NRF52
	__attribute__((used)) void MemoryManagement_Handler(){
		GS->ramRetainStructPtr->rebootReason = RebootReason::MEMORY_MANAGEMENT;
		NVIC_SystemReset();
	}
	__attribute__((used)) void BusFault_Handler(){
		GS->ramRetainStructPtr->rebootReason = RebootReason::BUS_FAULT;
		NVIC_SystemReset();
	}
	__attribute__((used)) void UsageFault_Handler(){
		GS->ramRetainStructPtr->rebootReason = RebootReason::USAGE_FAULT;
		NVIC_SystemReset();
	}
#endif



}

//################################################
#define __________________BOOTLOADER____________________

u32 FruityHal::GetBootloaderVersion()
{
	if(BOOTLOADER_UICR_ADDRESS != 0xFFFFFFFF){
		return *(u32*)(BOOTLOADER_UICR_ADDRESS + 1024);
	} else {
		return 0;
	}
}

u32 FruityHal::GetBootloaderAddress()
{
	return BOOTLOADER_UICR_ADDRESS;
}

ErrorType FruityHal::ActivateBootloaderOnReset()
{
#ifdef NRF52
	logt("DFUMOD", "Setting flags for nRF Secure DFU Bootloader");

	//Write a magic number into the retained register that will persists over reboots
	FruityHal::ClearGeneralPurposeRegister(0, 0xffffffff);
	FruityHal::WriteGeneralPurposeRegister(0, BOOTLOADER_DFU_START);

	FruityHal::ClearGeneralPurposeRegister(1, 0xffffffff);
	FruityHal::WriteGeneralPurposeRegister(1, BOOTLOADER_DFU_START2);

	// => After rebooting, the bootloader will check this register and will start the DFU process

	return ErrorType::SUCCESS;
#else
	return ErrorType::NOT_SUPPORTED;
#endif

}

//################################################
#define __________________MISC____________________


void FruityHal::SystemReset()
{
	sd_nvic_SystemReset();
}

// Retrieves the reboot reason from the RESETREAS register
RebootReason FruityHal::GetRebootReason()
{
#ifndef SIM_ENABLED
	u32 reason = NRF_POWER->RESETREAS;

	if(reason & POWER_RESETREAS_DOG_Msk){
		return RebootReason::WATCHDOG;
	} else if (reason & POWER_RESETREAS_RESETPIN_Msk){
		return RebootReason::PIN_RESET;
	} else if (reason & POWER_RESETREAS_OFF_Msk){
		return RebootReason::FROM_OFF_STATE;
	} else {
		return RebootReason::UNKNOWN;
	}
#else
	return (RebootReason)ST_getRebootReason();
#endif
}

//Clears the Reboot reason because the RESETREAS register is cumulative
u32 FruityHal::ClearRebootReason()
{
	sd_power_reset_reason_clr(0xFFFFFFFFUL);
	return 0;
}



u32 FruityHal::ClearGeneralPurposeRegister(u32 gpregId, u32 mask)
{
#ifdef NRF52
	return sd_power_gpregret_clr(gpregId, mask);
#elif NRF51
	return sd_power_gpregret_clr(gpregId);
#else
	return NRF_SUCCESS;
#endif
}

u32 FruityHal::WriteGeneralPurposeRegister(u32 gpregId, u32 mask)
{
#ifdef NRF52
	return sd_power_gpregret_set(gpregId, mask);
#elif NRF51
	return sd_power_gpregret_set(gpregId);
#else
	return NRF_SUCCESS;
#endif
}

bool FruityHal::setRetentionRegisterTwo(u8 val)
{
#ifdef NRF52
	nrf_power_gpregret2_set(val);
	return true;
#else
	return false;
#endif
}

//Starts the Watchdog with a static interval so that changing a config can do no harm
void FruityHal::StartWatchdog(bool safeBoot)
{
#if IS_ACTIVE(WATCHDOG)
	u32 err = 0;

	//Configure Watchdog to default: Run while CPU sleeps
	nrf_wdt_behaviour_set(NRF_WDT_BEHAVIOUR_RUN_SLEEP);
	//Configure Watchdog timeout
	if (!safeBoot) {
		nrf_wdt_reload_value_set(FM_WATCHDOG_TIMEOUT);
	}
	else {
		nrf_wdt_reload_value_set(FM_WATCHDOG_TIMEOUT_SAFE_BOOT);
	}
	// Configure Reload Channels
	nrf_wdt_reload_request_enable(NRF_WDT_RR0);

	//Enable
	nrf_wdt_task_trigger(NRF_WDT_TASK_START);

	logt("ERROR", "Watchdog started");
#endif
}

//Feeds the Watchdog to keep it quiet
void FruityHal::FeedWatchdog()
{
#if IS_ACTIVE(WATCHDOG)
	nrf_wdt_reload_request_set(NRF_WDT_RR0);
#endif
}

void FruityHal::DelayUs(u32 delayMicroSeconds)
{
#ifndef SIM_ENABLED
	nrf_delay_us(delayMicroSeconds);
#endif
}

void FruityHal::DelayMs(u32 delayMs)
{
#ifdef NRF51
	while(delayMs != 0)
	{
		delayMs--;
		nrf_delay_us(999);
	}
#else
	nrf_delay_ms(delayMs);
#endif
}

u32 FruityHal::EcbEncryptBlock(const u8 * p_key, const u8 * p_clearText, u8 * p_cipherText)
{
	u32 error;
	nrf_ecb_hal_data_t ecbData;
	CheckedMemset(&ecbData, 0x00, sizeof(ecbData));
	CheckedMemcpy(ecbData.key, p_key, SOC_ECB_KEY_LENGTH);
	CheckedMemcpy(ecbData.cleartext, p_clearText, SOC_ECB_CLEARTEXT_LENGTH);
	error = sd_ecb_block_encrypt(&ecbData);
	CheckedMemcpy(p_cipherText, ecbData.ciphertext, SOC_ECB_CIPHERTEXT_LENGTH);
	return error;
}

ErrorType FruityHal::FlashPageErase(u32 page)
{
	return nrfErrToGeneric(sd_flash_page_erase(page));
}

ErrorType FruityHal::FlashWrite(u32 * p_addr, u32 * p_data, u32 len)
{
	return nrfErrToGeneric(sd_flash_write((uint32_t *)p_addr, (uint32_t *)p_data, len));
}

void FruityHal::nvicEnableIRQ(u32 irqType)
{
#ifndef SIM_ENABLED
	sd_nvic_EnableIRQ((IRQn_Type)irqType);
#endif
}

void FruityHal::nvicDisableIRQ(u32 irqType)
{
#ifndef SIM_ENABLED
	sd_nvic_DisableIRQ((IRQn_Type)irqType);
#endif
}

void FruityHal::nvicSetPriorityIRQ(u32 irqType, u8 level)
{
#ifndef SIM_ENABLED
	sd_nvic_SetPriority((IRQn_Type)irqType, (uint32_t)level);
#endif
}

void FruityHal::nvicClearPendingIRQ(u32 irqType)
{
#ifndef SIM_ENABLED
	sd_nvic_ClearPendingIRQ((IRQn_Type)irqType);
#endif
}

#ifndef SIM_ENABLED
extern "C"{
//Eliminate Exception overhead when using pure virutal functions
//http://elegantinvention.com/blog/information/smaller-binary-size-with-c-on-baremetal-g/
	void __cxa_pure_virtual() {
		// Must never be called.
		logt("ERROR", "PVF call");
		constexpr u32 pureVirtualFunctionCalledError = 0xF002;
		APP_ERROR_CHECK(pureVirtualFunctionCalledError);
	}
}
#endif


// ######################### GPIO ############################
void FruityHal::GpioConfigureOutput(u32 pin)
{
	nrf_gpio_cfg_output(pin);
}

void FruityHal::GpioConfigureInput(u32 pin, GpioPullMode mode)
{
	nrf_gpio_cfg_input(pin, GenericPullModeToNrf(mode));
}

void FruityHal::GpioConfigureDefault(u32 pin)
{
	nrf_gpio_cfg_default(pin);
}

void FruityHal::GpioPinSet(u32 pin)
{
	nrf_gpio_pin_set(pin);
}

void FruityHal::GpioPinClear(u32 pin)
{
	nrf_gpio_pin_clear(pin);
}

void FruityHal::GpioPinToggle(u32 pin)
{
	nrf_gpio_pin_toggle(pin);
}

static void GpioteHandler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	for (u8 i = 0; i < MAX_GPIOTE_HANDLERS; i++)
	{
		if (((u32)pin == halMemory->GpioHandler[i].pin) && (halMemory->GpioHandler[i].handler != nullptr))
			halMemory->GpioHandler[i].handler((u32)pin, NrfPolarityToGeneric(action));
	}
}

ErrorType FruityHal::GpioConfigureInterrupt(u32 pin, FruityHal::GpioPullMode mode, FruityHal::GpioTransistion trigger, FruityHal::GpioInterruptHandler handler)
{
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	if ((handler == nullptr) || (halMemory->gpioteHandlersCreated == MAX_GPIOTE_HANDLERS)) return ErrorType::INVALID_PARAM;

	halMemory->GpioHandler[halMemory->gpioteHandlersCreated].handler = handler;
	halMemory->GpioHandler[halMemory->gpioteHandlersCreated++].pin = pin;
	ErrorType err = ErrorType::SUCCESS;
	nrf_drv_gpiote_in_config_t in_config;
	in_config.is_watcher = false;
	in_config.hi_accuracy = false;
	in_config.pull = GenericPullModeToNrf(mode);
	in_config.sense = GenericPolarityToNrf(trigger);
#if SDK == 15
	in_config.skip_gpio_setup = 0;
#endif

	err = nrfErrToGeneric(nrf_drv_gpiote_in_init(pin, &in_config, GpioteHandler));
	nrf_drv_gpiote_in_event_enable(pin, true);

	return err;
}

// ######################### ADC ############################

#ifndef SIM_ENABLED
FruityHal::AdcEventHandler AdcHandler;

#if defined(NRF52)
void SaadcCallback(nrf_drv_saadc_evt_t const * p_event)
{
	if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
	{
		AdcHandler();
	}
}
#else
void AdcCallback(nrf_drv_adc_evt_t const * p_event)
{
	if (p_event->type == NRF_DRV_ADC_EVT_DONE)
	{
		AdcHandler();
	}
}
#endif
#endif //SIM_ENABLED

ErrorType FruityHal::AdcInit(AdcEventHandler handler)
{
#ifndef SIM_ENABLED
	if (handler == nullptr) return ErrorType::INVALID_PARAM;
	AdcHandler = handler;

#if defined(NRF51)
	ret_code_t err_code;
	err_code = nrf_drv_adc_init(nullptr,AdcCallback);
	APP_ERROR_CHECK(err_code);
#else
	ret_code_t err_code;
	err_code = nrf_drv_saadc_init(nullptr,SaadcCallback);
	APP_ERROR_CHECK(err_code);
#endif
#endif //SIM_ENABLED
	return ErrorType::SUCCESS;
}

void FruityHal::AdcUninit()
{
#ifndef SIM_ENABLED
#if defined(NRF51)
	nrf_drv_adc_uninit();
#else
	nrf_drv_saadc_uninit();
#endif
#endif //SIM_ENABLED
}

#ifndef SIM_ENABLED
#ifdef NRF52
static nrf_saadc_input_t NrfPinToAnalogInput(u32 pin)
{
	switch (pin)
	{
		case 2:
			return NRF_SAADC_INPUT_AIN0;
		case 3:
			return NRF_SAADC_INPUT_AIN1;
		case 4:
			return NRF_SAADC_INPUT_AIN2;
		case 5:
			return NRF_SAADC_INPUT_AIN3;
		case 28:
			return NRF_SAADC_INPUT_AIN4;
		case 29:
			return NRF_SAADC_INPUT_AIN5;
		case 30:
			return NRF_SAADC_INPUT_AIN6;
		case 31:
			return NRF_SAADC_INPUT_AIN7;
		case 0xFF:
			return NRF_SAADC_INPUT_VDD;
		default:
			return NRF_SAADC_INPUT_DISABLED;
	}
}
#else
static nrf_adc_config_input_t NrfPinToAnalogInput(u32 pin)
{
	switch (pin)
	{
		case 26:
			return NRF_ADC_CONFIG_INPUT_0;
		case 27:
			return NRF_ADC_CONFIG_INPUT_1;
		case 1:
			return NRF_ADC_CONFIG_INPUT_2;
		case 2:
			return NRF_ADC_CONFIG_INPUT_3;
		case 3:
			return NRF_ADC_CONFIG_INPUT_4;
		case 4:
			return NRF_ADC_CONFIG_INPUT_5;
		case 5:
			return NRF_ADC_CONFIG_INPUT_6;
		case 6:
			return NRF_ADC_CONFIG_INPUT_7;
		default:
			return NRF_ADC_CONFIG_INPUT_DISABLED;
	}
}
#endif
#endif //SIM_ENABLED

ErrorType FruityHal::AdcConfigureChannel(u32 pin, AdcReference reference, AdcResoultion resolution, AdcGain gain)
{
#ifndef SIM_ENABLED
#if defined(NRF51)
	u32 nrfResolution = resolution == FruityHal::AdcResoultion::ADC_8_BIT ? NRF_ADC_CONFIG_RES_8BIT : NRF_ADC_CONFIG_RES_10BIT;
	u32 nrfGain = NRF_ADC_CONFIG_SCALING_INPUT_FULL_SCALE;
	u32 nrfReference = NRF_ADC_CONFIG_REF_VBG;
	switch (reference)
	{
		case FruityHal::AdcReference::ADC_REFERENCE_1_2V:
			nrfReference = NRF_ADC_CONFIG_REF_VBG;
			break;
		case FruityHal::AdcReference::ADC_REFERENCE_1_2_POWER_SUPPLY:
			nrfReference = NRF_ADC_CONFIG_REF_SUPPLY_ONE_HALF;
			break;
		case FruityHal::AdcReference::ADC_REFERENCE_1_3_POWER_SUPPLY:
			nrfReference = NRF_ADC_CONFIG_REF_SUPPLY_ONE_THIRD;
			break;
		case FruityHal::AdcReference::ADC_REFERENCE_0_6V:
		case FruityHal::AdcReference::ADC_REFERENCE_1_4_POWER_SUPPLY:
			return ErrorType::INVALID_PARAM;
	}
	switch (gain)
	{
		case FruityHal::AdcGain::ADC_GAIN_1:
			nrfGain = NRF_ADC_CONFIG_SCALING_INPUT_FULL_SCALE;
			break;
		case FruityHal::AdcGain::ADC_GAIN_2_3:
			nrfGain = NRF_ADC_CONFIG_SCALING_INPUT_TWO_THIRDS;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1_3:
			nrfGain = NRF_ADC_CONFIG_SCALING_INPUT_ONE_THIRD;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1_6:
		case FruityHal::AdcGain::ADC_GAIN_1_5:
		case FruityHal::AdcGain::ADC_GAIN_1_4:
		case FruityHal::AdcGain::ADC_GAIN_1_2:
		case FruityHal::AdcGain::ADC_GAIN_2:
		case FruityHal::AdcGain::ADC_GAIN_4:
			return ErrorType::INVALID_PARAM;
	}
	nrf_drv_adc_channel_t adc_channel_config;
	nrf_drv_adc_channel_config_t cct;
	cct.resolution = nrfResolution;
	cct.input = nrfGain ;
	cct.reference = nrfReference;
	cct.ain = NrfPinToAnalogInput(pin);
	adc_channel_config.config.config = cct;
	adc_channel_config.p_next = nullptr;
	nrf_drv_adc_config_t adc_config;
	adc_config.interrupt_priority = ADC_CONFIG_IRQ_PRIORITY;             //Get default ADC configuration
	nrf_drv_adc_channel_enable(&adc_channel_config);                          //Configure and enable an ADC channel
#endif

//#define NRF52
#if defined(NRF52)
	nrf_saadc_resolution_t nrfResolution = resolution == FruityHal::AdcResoultion::ADC_8_BIT ? NRF_SAADC_RESOLUTION_8BIT : NRF_SAADC_RESOLUTION_10BIT;
	nrf_saadc_gain_t nrfGain = NRF_SAADC_GAIN1_6;
	nrf_saadc_reference_t nrfReference = NRF_SAADC_REFERENCE_VDD4;
	switch (reference)
	{
		case FruityHal::AdcReference::ADC_REFERENCE_0_6V:
			nrfReference = NRF_SAADC_REFERENCE_INTERNAL;
			break;
		case FruityHal::AdcReference::ADC_REFERENCE_1_4_POWER_SUPPLY:
			nrfReference = NRF_SAADC_REFERENCE_VDD4;
			break;
		case FruityHal::AdcReference::ADC_REFERENCE_1_2V:
		case FruityHal::AdcReference::ADC_REFERENCE_1_2_POWER_SUPPLY:
		case FruityHal::AdcReference::ADC_REFERENCE_1_3_POWER_SUPPLY:
			return ErrorType::INVALID_PARAM;
	}
	switch (gain)
	{
		case FruityHal::AdcGain::ADC_GAIN_1_6:
			nrfGain = NRF_SAADC_GAIN1_6;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1_5:
			nrfGain = NRF_SAADC_GAIN1_5;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1_4:
			nrfGain = NRF_SAADC_GAIN1_4;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1_3:
			nrfGain = NRF_SAADC_GAIN1_3;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1_2:
			nrfGain = NRF_SAADC_GAIN1_2;
			break;
		case FruityHal::AdcGain::ADC_GAIN_1:
			nrfGain = NRF_SAADC_GAIN1;
			break;
		case FruityHal::AdcGain::ADC_GAIN_2:
			nrfGain = NRF_SAADC_GAIN2;
			break;
		case FruityHal::AdcGain::ADC_GAIN_4:
			nrfGain = NRF_SAADC_GAIN4;
			break;
		case FruityHal::AdcGain::ADC_GAIN_2_3:
			return ErrorType::INVALID_PARAM;
	}
	ret_code_t err_code;
	nrf_saadc_channel_config_t channel_config;

	channel_config = NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NrfPinToAnalogInput(pin));
	channel_config.gain = nrfGain;
	channel_config.reference = nrfReference;
	err_code = nrf_drv_saadc_channel_init(0, &channel_config);
	APP_ERROR_CHECK(err_code);
	nrf_saadc_resolution_set(nrfResolution);
#endif
#endif //SIM_ENABLED
	return ErrorType::SUCCESS;
}

ErrorType FruityHal::AdcSample(i16 & buffer, u8 len)
{
	u32 err = NRF_SUCCESS;
#ifndef SIM_ENABLED
#if defined(NRF51)
	err = nrf_drv_adc_buffer_convert(&buffer, len);
	if (err == NRF_SUCCESS) nrf_drv_adc_sample();
#endif

#if defined(NRF52)
	err = nrf_drv_saadc_buffer_convert(&buffer, len);
	if (err == NRF_SUCCESS)
	{
		err = nrf_drv_saadc_sample(); // Non-blocking triggering of SAADC Sampling
	} 
#endif
#endif //SIM_ENABLED
	return nrfErrToGeneric(err);
}

u8 FruityHal::AdcConvertSampleToDeciVoltage(u32 sample)
{
#if defined(NRF52)
constexpr double REF_VOLTAGE_INTERNAL_IN_MILLI_VOLTS = 600; // Maximum Internal Reference Voltage
constexpr double VOLTAGE_DIVIDER_INTERNAL_IN_MILLI_VOLTS = 166; //Internal voltage divider
constexpr double ADC_RESOLUTION_10BIT = 1023;
return sample * REF_VOLTAGE_INTERNAL_IN_MILLI_VOLTS / (VOLTAGE_DIVIDER_INTERNAL_IN_MILLI_VOLTS*ADC_RESOLUTION_10BIT)*10;
#else
constexpr int REF_VOLTAGE_IN_MILLIVOLTS = 1200;
return sample * REF_VOLTAGE_IN_MILLIVOLTS / 1024;
#endif
}

u8 FruityHal::AdcConvertSampleToDeciVoltage(u32 sample, u16 voltageDivider)
{
#if defined(NRF52)
constexpr double REF_VOLTAGE_EXTERNAL_IN_MILLI_VOLTS = 825; // Maximum Internal Reference Voltage
constexpr double VOLTAGE_GAIN_IN_MILLI_VOLTS = 200; //Internal voltage divider
constexpr double ADC_RESOLUTION_10BIT = 1023;
double result = sample * (REF_VOLTAGE_EXTERNAL_IN_MILLI_VOLTS/VOLTAGE_GAIN_IN_MILLI_VOLTS) * (1/ADC_RESOLUTION_10BIT) * (voltageDivider);
return (u8)result;
#else
return 0;
#endif
}

#define __________________CONVERT____________________

ble_gap_addr_t FruityHal::Convert(const FruityHal::BleGapAddr* address)
{
	ble_gap_addr_t addr;
	CheckedMemset(&addr, 0x00, sizeof(addr));
	CheckedMemcpy(addr.addr, address->addr, FH_BLE_GAP_ADDR_LEN);
	addr.addr_type = (u8)address->addr_type;
#ifdef NRF52
	addr.addr_id_peer = 0;
#endif
	return addr;
}
FruityHal::BleGapAddr FruityHal::Convert(const ble_gap_addr_t* p_addr)
{
	FruityHal::BleGapAddr address;
	CheckedMemset(&address, 0x00, sizeof(address));
	CheckedMemcpy(address.addr, p_addr->addr, FH_BLE_GAP_ADDR_LEN);
	address.addr_type = (BleGapAddrType)p_addr->addr_type;

	return address;
}

u8 FruityHal::ConvertPortToGpio(u8 port, u8 pin)
{
#ifdef NRF52
	return NRF_GPIO_PIN_MAP(port, pin);
#elif defined NRF51 || defined SIM_ENABLED
	return pin;
#else
	static_assert(false,"Convertion is not yet defined for this board");
#endif
}

void FruityHal::disableHardwareDfuBootloader()
{
#ifndef SIM_ENABLED
	bool bootloaderAvailable = (FruityHal::GetBootloaderAddress() != 0xFFFFFFFF);
	u32 bootloaderAddress = FruityHal::GetBootloaderAddress();

	//Check if a bootloader exists
	if (bootloaderAddress != 0xFFFFFFFFUL) {
		u32* magicNumberAddress = (u32*)NORDIC_DFU_MAGIC_NUMBER_ADDRESS;
		//Check if the magic number is currently set to enable nordic dfu
		if (*magicNumberAddress == ENABLE_NORDIC_DFU_MAGIC_NUMBER) {
			logt("WARNING", "Disabling nordic dfu");

			//Overwrite the magic number so that the nordic dfu will be inactive afterwards
			u32 data = 0x00;
			GS->flashStorage.CacheAndWriteData(&data, magicNumberAddress, sizeof(u32), nullptr, 0);
		}
	}
#endif
}

u32 FruityHal::getMasterBootRecordSize()
{
#ifdef SIM_ENABLED
	return 1024 * 4;
#else
	return MBR_SIZE;
#endif
}

u32 FruityHal::getSoftDeviceSize()
{
#ifdef SIM_ENABLED
	//Even though the soft device size is not strictly dependent on the chipset, it is a good approximation.
	//These values were measured on real hardware on 26.09.2019.
	switch (GET_CHIPSET())
	{
	case Chipset::CHIP_NRF51:
		return 110592;
	case Chipset::CHIP_NRF52:
	case Chipset::CHIP_NRF52840:
		return 143360;
	default:
		SIMEXCEPTION(IllegalStateException);
	}
	return 0;
#else
	return SD_SIZE_GET(MBR_SIZE);
#endif
}

u32 FruityHal::GetSoftDeviceVersion()
{
#ifdef SIM_ENABLED
	switch (GetBleStackType())
	{
	case BleStackType::NRF_SD_130_ANY:
		return 0x0087;
	case BleStackType::NRF_SD_132_ANY:
		return 0x00A8;
	case BleStackType::NRF_SD_140_ANY:
		return 0x00A9;
	default:
		SIMEXCEPTION(IllegalStateException);
	}
	return 0;
#else
	return SD_FWID_GET(MBR_SIZE);
#endif
}

BleStackType FruityHal::GetBleStackType()
{
	//TODO: We can later also determine the exact version of the stack if necessary
	//It is however not easy to implement this for Nordic as there is no public list
	//available.
#ifdef SIM_ENABLED
	return (BleStackType)sim_get_stack_type();
#elif defined(S130)
	return BleStackType::NRF_SD_130_ANY;
#elif defined(S132)
	return BleStackType::NRF_SD_132_ANY;
#elif defined(S140)
	return BleStackType::NRF_SD_140_ANY;
#else
#error "Unsupported Stack"
#endif
}

void FruityHal::bleStackErrorHandler(u32 id, u32 info)
{
	switch (id) {
		case NRF_FAULT_ID_SD_RANGE_START: //Softdevice Asserts, info is nullptr
		{
			break;
		}
		case NRF_FAULT_ID_APP_RANGE_START: //Application asserts (e.g. wrong memory access), info is memory address
		{
			GS->ramRetainStructPtr->code2 = info;
			break;
		}
		case NRF_FAULT_ID_SDK_ASSERT: //SDK asserts
		{
			GS->ramRetainStructPtr->code2 = ((assert_info_t *)info)->line_num;
			u8 len = (u8)strlen((const char*)((assert_info_t *)info)->p_file_name);
			if (len > (RAM_PERSIST_STACKSTRACE_SIZE - 1) * 4) len = (RAM_PERSIST_STACKSTRACE_SIZE - 1) * 4;
			CheckedMemcpy(GS->ramRetainStructPtr->stacktrace + 1, ((assert_info_t *)info)->p_file_name, len);
			break;
		}
		case NRF_FAULT_ID_SDK_ERROR: //SDK errors
		{
			GS->ramRetainStructPtr->code2 = ((error_info_t *)info)->line_num;
			GS->ramRetainStructPtr->code3 = ((error_info_t *)info)->err_code;

			//Copy filename to stacktrace
			u8 len = (u8)strlen((const char*)((error_info_t *)info)->p_file_name);
			if (len > (RAM_PERSIST_STACKSTRACE_SIZE - 1) * 4) len = (RAM_PERSIST_STACKSTRACE_SIZE - 1) * 4;
			CheckedMemcpy(GS->ramRetainStructPtr->stacktrace + 1, ((error_info_t *)info)->p_file_name, len);
			break;
		}
	}
}

const char* FruityHal::getBleEventNameString(u16 bleEventId)
{
#if defined(TERMINAL_ENABLED)
	switch (bleEventId)
	{
#if defined(NRF51) || defined SIM_ENABLED
	case BLE_EVT_TX_COMPLETE:
		return "BLE_EVT_TX_COMPLETE";
#endif
	case BLE_EVT_USER_MEM_REQUEST:
		return "BLE_EVT_USER_MEM_REQUEST";
	case BLE_EVT_USER_MEM_RELEASE:
		return "BLE_EVT_USER_MEM_RELEASE";
	case BLE_GAP_EVT_CONNECTED:
		return "BLE_GAP_EVT_CONNECTED";
	case BLE_GAP_EVT_DISCONNECTED:
		return "BLE_GAP_EVT_DISCONNECTED";
	case BLE_GAP_EVT_CONN_PARAM_UPDATE:
		return "BLE_GAP_EVT_CONN_PARAM_UPDATE";
	case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
		return "BLE_GAP_EVT_SEC_PARAMS_REQUEST";
	case BLE_GAP_EVT_SEC_INFO_REQUEST:
		return "BLE_GAP_EVT_SEC_INFO_REQUEST";
	case BLE_GAP_EVT_PASSKEY_DISPLAY:
		return "BLE_GAP_EVT_PASSKEY_DISPLAY";
	case BLE_GAP_EVT_AUTH_KEY_REQUEST:
		return "BLE_GAP_EVT_AUTH_KEY_REQUEST";
	case BLE_GAP_EVT_AUTH_STATUS:
		return "BLE_GAP_EVT_AUTH_STATUS";
	case BLE_GAP_EVT_CONN_SEC_UPDATE:
		return "BLE_GAP_EVT_CONN_SEC_UPDATE";
	case BLE_GAP_EVT_TIMEOUT:
		return "BLE_GAP_EVT_TIMEOUT";
	case BLE_GAP_EVT_RSSI_CHANGED:
		return "BLE_GAP_EVT_RSSI_CHANGED";
	case BLE_GAP_EVT_ADV_REPORT:
		return "BLE_GAP_EVT_ADV_REPORT";
	case BLE_GAP_EVT_SEC_REQUEST:
		return "BLE_GAP_EVT_SEC_REQUEST";
	case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
		return "BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST";
	case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:
		return "BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP";
	case BLE_GATTC_EVT_REL_DISC_RSP:
		return "BLE_GATTC_EVT_REL_DISC_RSP";
	case BLE_GATTC_EVT_CHAR_DISC_RSP:
		return "BLE_GATTC_EVT_CHAR_DISC_RSP";
	case BLE_GATTC_EVT_DESC_DISC_RSP:
		return "BLE_GATTC_EVT_DESC_DISC_RSP";
	case BLE_GATTC_EVT_CHAR_VAL_BY_UUID_READ_RSP:
		return "BLE_GATTC_EVT_CHAR_VAL_BY_UUID_READ_RSP";
	case BLE_GATTC_EVT_READ_RSP:
		return "BLE_GATTC_EVT_READ_RSP";
	case BLE_GATTC_EVT_CHAR_VALS_READ_RSP:
		return "BLE_GATTC_EVT_CHAR_VALS_READ_RSP";
	case BLE_GATTC_EVT_WRITE_RSP:
		return "BLE_GATTC_EVT_WRITE_RSP";
	case BLE_GATTC_EVT_HVX:
		return "BLE_GATTC_EVT_HVX";
	case BLE_GATTC_EVT_TIMEOUT:
		return "BLE_GATTC_EVT_TIMEOUT";
	case BLE_GATTS_EVT_WRITE:
		return "BLE_GATTS_EVT_WRITE";
	case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
		return "BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST";
	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		return "BLE_GATTS_EVT_SYS_ATTR_MISSING";
	case BLE_GATTS_EVT_HVC:
		return "BLE_GATTS_EVT_HVC";
	case BLE_GATTS_EVT_SC_CONFIRM:
		return "BLE_GATTS_EVT_SC_CONFIRM";
	case BLE_GATTS_EVT_TIMEOUT:
		return "BLE_GATTS_EVT_TIMEOUT";
#ifdef NRF52
	case BLE_GATTS_EVT_HVN_TX_COMPLETE:
		return "BLE_GATTS_EVT_HVN_TX_COMPLETE";
	case BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE:
		return "BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE";
	case BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST:
		return "BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST";
	case BLE_GAP_EVT_DATA_LENGTH_UPDATE:
		return "BLE_GAP_EVT_DATA_LENGTH_UPDATE";
	case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
		return "BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST";
	case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:
		return "BLE_GATTC_EVT_EXCHANGE_MTU_RSP";
	case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
		return "BLE_GAP_EVT_PHY_UPDATE_REQUEST";
#endif
	default:
		SIMEXCEPTION(ErrorCodeUnknownException); //Could be an error or should be added to the list
		return "UNKNOWN_EVENT";
	}
#else
	return nullptr;
#endif
}

ErrorType FruityHal::getDeviceConfiguration(DeviceConfiguration & config)
{
	//We are using a magic number to determine if the UICR data present was put there by fruitydeploy
	if (NRF_UICR->CUSTOMER[0] == UICR_SETTINGS_MAGIC_WORD) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
		// The following casts away volatile, which is intended behavior and is okay, as the CUSTOMER data won't change during runtime.
		CheckedMemcpy(&config, (u32*)NRF_UICR->CUSTOMER, sizeof(DeviceConfiguration));
		return ErrorType::SUCCESS;
#pragma GCC diagnostic pop
	}
	else if(GS->recordStorage.IsInit()){
		//On some devices, we are not able to store data in UICR as they are flashed by a 3rd party
		//and we are only updating to fruitymesh. We have a dedicated record for these instances
		//which is used the same as if the data were stored in UICR
		SizedData data = GS->recordStorage.GetRecordData(RECORD_STORAGE_RECORD_ID_UICR_REPLACEMENT);
		if (data.length >= 16 * 4 && ((u32*)data.data)[0] == UICR_SETTINGS_MAGIC_WORD) {
			CheckedMemcpy(&config, (u32*)data.data, sizeof(DeviceConfiguration));
			return ErrorType::SUCCESS;
		}
	}

	return ErrorType::INVALID_STATE;
}

u32 * FruityHal::GetUserMemoryAddress()
{
	return (u32 *)NRF_UICR;
}

u32 * FruityHal::GetDeviceMemoryAddress()
{
	return (u32 *)NRF_FICR;
}

u32 FruityHal::GetCodePageSize()
{
	return NRF_FICR->CODEPAGESIZE;
}

u32 FruityHal::GetCodeSize()
{
	return NRF_FICR->CODESIZE;
}

u32 FruityHal::GetDeviceId()
{
	return NRF_FICR->DEVICEID[0];
}

void FruityHal::GetDeviceAddress(u8 * p_address)
{
	for (u32 i = 0; i < 8; i++)
	{
		// Not CheckedMemcpy, as DEVICEADDR is volatile.
		p_address[i] = ((const volatile u8*)NRF_FICR->DEVICEADDR)[i];
	}
}

void FruityHal::disableUart()
{
#ifndef SIM_ENABLED
	//Disable UART interrupt
	sd_nvic_DisableIRQ(UART0_IRQn);

	//Disable all UART Events
	nrf_uart_int_disable(NRF_UART0, NRF_UART_INT_MASK_RXDRDY |
		NRF_UART_INT_MASK_TXDRDY |
		NRF_UART_INT_MASK_ERROR |
		NRF_UART_INT_MASK_RXTO);
	//Clear all pending events
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_CTS);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_NCTS);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXDRDY);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_TXDRDY);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);

	//Disable UART
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Disabled;

	//Reset all Pinx to default state
	nrf_uart_txrx_pins_disconnect(NRF_UART0);
	nrf_uart_hwfc_pins_disconnect(NRF_UART0);

	nrf_gpio_cfg_default(Boardconfig->uartTXPin);
	nrf_gpio_cfg_default(Boardconfig->uartRXPin);

	if (Boardconfig->uartRTSPin != -1) {
		if (NRF_UART0->PSELRTS != NRF_UART_PSEL_DISCONNECTED) nrf_gpio_cfg_default(Boardconfig->uartRTSPin);
		if (NRF_UART0->PSELCTS != NRF_UART_PSEL_DISCONNECTED) nrf_gpio_cfg_default(Boardconfig->uartCTSPin);
	}
#endif
}

void FruityHal::UartHandleError(u32 error)
{
	//Errorsource is given, but has to be cleared to be handled
	NRF_UART0->ERRORSRC = error;

	//FIXME: maybe we need some better error handling here
}

bool FruityHal::UartCheckInputAvailable()
{
	return NRF_UART0->EVENTS_RXDRDY == 1;
}

FruityHal::UartReadCharBlockingResult FruityHal::UartReadCharBlocking()
{
	UartReadCharBlockingResult retVal;

#if IS_INACTIVE(GW_SAVE_SPACE)
	while (NRF_UART0->EVENTS_RXDRDY != 1) {
		if (NRF_UART0->EVENTS_ERROR) {
			FruityHal::UartHandleError(NRF_UART0->ERRORSRC);
			retVal.didError = true;
		}
		// Info: No timeout neede here, as we are waiting for user input
	}
	NRF_UART0->EVENTS_RXDRDY = 0;
	retVal.c = NRF_UART0->RXD;
#endif

	return retVal;
}

void FruityHal::UartPutStringBlockingWithTimeout(const char* message)
{
	uint_fast8_t i = 0;
	uint8_t byte = message[i++];

	while (byte != '\0')
	{
		NRF_UART0->TXD = byte;
		byte = message[i++];

		int i = 0;
		while (NRF_UART0->EVENTS_TXDRDY != 1) {
			//Timeout if it was not possible to put the character
			if (i > 10000) {
				return;
			}
			i++;
			//FIXME: Do we need error handling here? Will cause lost characters
		}
		NRF_UART0->EVENTS_TXDRDY = 0;
	}
}

bool FruityHal::IsUartErroredAndClear()
{
	if (nrf_uart_int_enable_check(NRF_UART0, NRF_UART_INT_MASK_ERROR) &&
		nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_ERROR))
	{
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);

		FruityHal::UartHandleError(NRF_UART0->ERRORSRC);

		return true;
	}
	return false;
}

bool FruityHal::IsUartTimedOutAndClear()
{
	if (nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_RXTO))
	{
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);

		//Restart transmission and clear previous buffer
		nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTRX);

		return true;

		//TODO: can we check if this works???
	}
	return false;
}

FruityHal::UartReadCharResult FruityHal::UartReadChar()
{
	UartReadCharResult retVal;

	if (nrf_uart_int_enable_check(NRF_UART0, NRF_UART_INT_MASK_RXDRDY) &&
		nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_RXDRDY))
	{
		//Reads the byte
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXDRDY);
#ifndef SIM_ENABLED
		retVal.c = NRF_UART0->RXD;
#else
		retVal.c = nrf_uart_rxd_get(NRF_UART0);
#endif
		retVal.hasNewChar = true;

		//Disable the interrupt to stop receiving until instructed further
		nrf_uart_int_disable(NRF_UART0, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_ERROR);
	}

	return retVal;
}

void FruityHal::UartEnableReadInterrupt()
{
	nrf_uart_int_enable(NRF_UART0, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_ERROR);
}

void FruityHal::EnableUart(bool promptAndEchoMode)
{
	//Configure pins
	nrf_gpio_pin_set(Boardconfig->uartTXPin);
	nrf_gpio_cfg_output(Boardconfig->uartTXPin);
	nrf_gpio_cfg_input(Boardconfig->uartRXPin, NRF_GPIO_PIN_NOPULL);

	nrf_uart_baudrate_set(NRF_UART0, (nrf_uart_baudrate_t)Boardconfig->uartBaudRate);
	nrf_uart_configure(NRF_UART0, NRF_UART_PARITY_EXCLUDED, Boardconfig->uartRTSPin != -1 ? NRF_UART_HWFC_ENABLED : NRF_UART_HWFC_DISABLED);
	nrf_uart_txrx_pins_set(NRF_UART0, Boardconfig->uartTXPin, Boardconfig->uartRXPin);

	//Configure RTS/CTS (if RTS is -1, disable flow control)
	if (Boardconfig->uartRTSPin != -1) {
		nrf_gpio_cfg_input(Boardconfig->uartCTSPin, NRF_GPIO_PIN_NOPULL);
		nrf_gpio_pin_set(Boardconfig->uartRTSPin);
		nrf_gpio_cfg_output(Boardconfig->uartRTSPin);
		nrf_uart_hwfc_pins_set(NRF_UART0, Boardconfig->uartRTSPin, Boardconfig->uartCTSPin);
	}

	if (!promptAndEchoMode)
	{
		//Enable Interrupts + timeout events
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);
		nrf_uart_int_enable(NRF_UART0, NRF_UART_INT_MASK_RXTO);

		sd_nvic_SetPriority(UART0_IRQn, APP_IRQ_PRIORITY_LOW);
		sd_nvic_ClearPendingIRQ(UART0_IRQn);
		sd_nvic_EnableIRQ(UART0_IRQn);
	}

	//Enable UART
	nrf_uart_enable(NRF_UART0);

	//Enable Receiver
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXDRDY);
	nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTRX);

	//Enable Transmitter
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_TXDRDY);
	nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTTX);

	if (!promptAndEchoMode)
	{
		//Start receiving RX events
		FruityHal::UartEnableReadInterrupt();
	}
}

bool FruityHal::checkAndHandleUartTimeout()
{
#ifndef SIM_ENABLED
	if (nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_RXTO))
	{
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);

		//Restart transmission and clear previous buffer
		nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTRX);

		return true;
	}
#endif

	return false;
}

u32 FruityHal::checkAndHandleUartError()
{
	//Checks if an error occured
	if (nrf_uart_int_enable_check(NRF_UART0, NRF_UART_INT_MASK_ERROR) &&
		nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_ERROR))
	{
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);

		//Errorsource is given, but has to be cleared to be handled
		NRF_UART0->ERRORSRC = NRF_UART0->ERRORSRC;

		return NRF_UART0->ERRORSRC;
	}
	return 0;
}

// We only need twi and spi for asset and there is no target for nrf51
#if !defined(NRF51) && defined(ACTIVATE_ASSET_MODULE)
#ifndef SIM_ENABLED
#define TWI_INSTANCE_ID     1
static constexpr nrf_drv_twi_t twi = NRF_DRV_TWI_INSTANCE(TWI_INSTANCE_ID);
#endif

#ifndef SIM_ENABLED
static void twi_handler(nrf_drv_twi_evt_t const * pEvent, void * pContext)
{
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	switch (pEvent->type) {
	// Transfer completed event.
	case NRF_DRV_TWI_EVT_DONE:
		halMemory->twiXferDone = true;
		break;

	// NACK received after sending the address
	case NRF_DRV_TWI_EVT_ADDRESS_NACK:
		break;

	// NACK received after sending a data byte.
	case NRF_DRV_TWI_EVT_DATA_NACK:
		break;

	default:
		break;
	}
}
#endif

ErrorType FruityHal::twi_init(i32 sclPin, i32 sdaPin)
{
	u32 errCode = NRF_SUCCESS;
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
#ifndef SIM_ENABLED
	// twi.reg          = {NRF_DRV_TWI_PERIPHERAL(TWI_INSTANCE_ID)};
	// twi.drv_inst_idx = CONCAT_3(TWI, TWI_INSTANCE_ID, _INSTANCE_INDEX);
	// twi.use_easy_dma = TWI_USE_EASY_DMA(TWI_INSTANCE_ID);

	const nrf_drv_twi_config_t twiConfig = {
			.scl                = (u32)sclPin,
			.sda                = (u32)sdaPin,
#if SDK == 15
			.frequency          = NRF_DRV_TWI_FREQ_250K,
#else
			.frequency          = NRF_TWI_FREQ_250K,
#endif
			.interrupt_priority = APP_IRQ_PRIORITY_HIGH,
			.clear_bus_init     = false,
			.hold_bus_uninit    = false
	};

	errCode = nrf_drv_twi_init(&twi, &twiConfig, twi_handler, NULL);
	if (errCode != NRF_ERROR_INVALID_STATE && errCode != NRF_SUCCESS) {
		APP_ERROR_CHECK(errCode);
	}

	nrf_drv_twi_enable(&twi);

	halMemory->twiInitDone = true;
#else
	errCode = cherrySimInstance->currentNode->twiWasInit ? NRF_ERROR_INVALID_STATE : NRF_SUCCESS;
	if (cherrySimInstance->currentNode->twiWasInit)
	{
		//Was already initialized!
		SIMEXCEPTION(IllegalStateException);
	}
	cherrySimInstance->currentNode->twiWasInit = true;
#endif
	return nrfErrToGeneric(errCode);
}

void FruityHal::twi_uninit(i32 sclPin, i32 sdaPin)
{
#ifndef SIM_ENABLED
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	nrf_drv_twi_disable(&twi);
	nrf_drv_twi_uninit(&twi);
	nrf_gpio_cfg_default((u32)sclPin);
	nrf_gpio_cfg_default((u32)sdaPin);
	NRF_TWI1->ENABLE = 0;

	halMemory->twiInitDone = false;
#endif
}

void FruityHal::twi_gpio_address_pin_set_and_wait(bool high, i32 sdaPin)
{
#ifndef SIM_ENABLED
	nrf_gpio_cfg_output((u32)sdaPin);
	if (high) {
		nrf_gpio_pin_set((u32)sdaPin);
		nrf_delay_us(200);
	}
	else {
		nrf_gpio_pin_clear((u32)sdaPin);
		nrf_delay_us(200);
	}

	nrf_gpio_pin_set((u32)sdaPin);
#endif
}

ErrorType FruityHal::twi_registerWrite(u8 slaveAddress, u8 const * pTransferData, u8 length)
{
	// Slave Address (Command) (7 Bit) + WriteBit (1 Bit) + register Byte (1 Byte) + Data (n Bytes)

	u32 errCode = NRF_SUCCESS;
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
#ifndef SIM_ENABLED
	halMemory->twiXferDone = false;

	errCode =  nrf_drv_twi_tx(&twi, slaveAddress, pTransferData, length, false);

	if (errCode != NRF_SUCCESS)
	{
		return nrfErrToGeneric(errCode);
	}
	// wait for transmission complete
	while(halMemory->twiXferDone == false);
	halMemory->twiXferDone = false;
#endif
	return nrfErrToGeneric(errCode);
}

ErrorType FruityHal::twi_registerRead(u8 slaveAddress, u8 reg, u8 * pReceiveData, u8 length)
{
	// Slave Address (7 Bit) (Command) + WriteBit (1 Bit) + register Byte (1 Byte) + Repeated Start + Slave Address + ReadBit + Data.... + nAck
	u32 errCode = NRF_SUCCESS;
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
#ifndef SIM_ENABLED
	halMemory->twiXferDone = false;

	nrf_drv_twi_xfer_desc_t xfer = NRF_DRV_TWI_XFER_DESC_TXRX(slaveAddress, &reg, 1, pReceiveData, length);

	errCode = nrf_drv_twi_xfer(&twi, &xfer, 0);

	if (errCode != NRF_SUCCESS)
	{
		return nrfErrToGeneric(errCode);
	}

	// wait for transmission and read complete
	while(halMemory->twiXferDone == false);
	halMemory->twiXferDone = false;
#endif
	return nrfErrToGeneric(errCode);
}

bool FruityHal::twi_isInitialized(void)
{
#ifndef SIM_ENABLED
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	return halMemory->twiInitDone;
#else
	return cherrySimInstance->currentNode->twiWasInit;
#endif
}

ErrorType FruityHal::twi_read(u8 slaveAddress, u8 * pReceiveData, u8 length)
{
	// Slave Address (7 Bit) (Command) + ReadBit (1 Bit) + Data.... + nAck

	u32 errCode = NRF_SUCCESS;
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
#ifndef SIM_ENABLED
	halMemory->twiXferDone = false;

	nrf_drv_twi_xfer_desc_t xfer;// = NRF_DRV_TWI_XFER_DESC_RX(slaveAddress, pReceiveData, length);
	CheckedMemset(&xfer, 0x00, sizeof(xfer));
	xfer.type = NRF_DRV_TWI_XFER_RX;
	xfer.address = slaveAddress;
	xfer.primary_length = length;
	xfer.p_primary_buf  = pReceiveData;

	errCode = nrf_drv_twi_xfer(&twi, &xfer, 0);

	if (errCode != NRF_SUCCESS)
	{
		return nrfErrToGeneric(errCode);
	}

	// wait for transmission and read complete
	while(halMemory->twiXferDone == false);
	halMemory->twiXferDone = false;
#endif
	return nrfErrToGeneric(errCode);
}

#ifndef SIM_ENABLED
#define SPI_INSTANCE  0
static constexpr nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE);
#endif

#ifndef SIM_ENABLED
void spi_event_handler(nrf_drv_spi_evt_t const * p_event, void* p_context)
{
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	halMemory->spiXferDone = true;
	logt("FH", "SPI Xfer done");
}
#endif

void FruityHal::spi_init(i32 sckPin, i32 misoPin, i32 mosiPin)
{
#ifndef SIM_ENABLED
	/* Conigure SPI Interface */
	nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
	spi_config.sck_pin = (u32)sckPin;
	spi_config.miso_pin = (u32)misoPin;
	spi_config.mosi_pin = (u32)mosiPin;
	spi_config.frequency = NRF_DRV_SPI_FREQ_4M;

	APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, spi_event_handler,NULL));

	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	halMemory->spiXferDone = true;
	halMemory->spiInitDone = true;
#else
	if (cherrySimInstance->currentNode->spiWasInit)
	{
		//Already initialized!
		SIMEXCEPTION(IllegalStateException);
	}
	cherrySimInstance->currentNode->spiWasInit = true;
#endif

}


bool FruityHal::spi_isInitialized(void)
{
#ifndef SIM_ENABLED
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	return halMemory->spiInitDone;
#else
	return cherrySimInstance->currentNode->spiWasInit;
#endif
}

ErrorType FruityHal::spi_transfer(u8* const p_toWrite, u8 count, u8* const p_toRead, i32 slaveSelectPin)
{
	u32 retVal = NRF_SUCCESS;
#ifndef SIM_ENABLED
	NrfHalMemory* halMemory = (NrfHalMemory*)GS->halMemory;
	logt("FH", "Transferring to BME");

	if ((NULL == p_toWrite) || (NULL == p_toRead))
	{
		retVal = NRF_ERROR_INTERNAL;
	}


	/* check if an other SPI transfer is running */
	if ((true == halMemory->spiXferDone) && (NRF_SUCCESS == retVal))
	{
		halMemory->spiXferDone = false;

		nrf_gpio_pin_clear((u32)slaveSelectPin);
		APP_ERROR_CHECK(nrf_drv_spi_transfer(&spi, p_toWrite, count, p_toRead, count));
		//Locks if run in interrupt context
		while (!halMemory->spiXferDone)
		{
			sd_app_evt_wait();
		}
		nrf_gpio_pin_set((u32)slaveSelectPin);
		retVal = NRF_SUCCESS;
	}
	else
	{
		retVal = NRF_ERROR_BUSY;
	}
#endif
	return nrfErrToGeneric(retVal);
}

void FruityHal::spi_configureSlaveSelectPin(i32 pin)
{
#ifndef SIM_ENABLED
	nrf_gpio_pin_dir_set((u32)pin, NRF_GPIO_PIN_DIR_OUTPUT);
	nrf_gpio_cfg_output((u32)pin);
	nrf_gpio_pin_set((u32)pin);
#endif
}
#endif // ifndef NRF51

u32 FruityHal::GetHalMemorySize()
{
	return sizeof(NrfHalMemory);
}
