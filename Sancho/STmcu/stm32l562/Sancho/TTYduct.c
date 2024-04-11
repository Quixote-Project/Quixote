/** \file
 * This file provides the implementation of an object that provides
 * packet based communications over a tty device using interrupt
 * based input.
 */

/**************************************************************************
 * (C)Copyright 2021, Enjellic Systems Development, LLC. All rights reserved.
 **************************************************************************/

/* Include files. */
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <stm32l5xx_hal_dma.h>
#include <stm32l5xx_hal_uart.h>

#include <HurdLib.h>

#include <Origin.h>
#include <Buffer.h>
#include <String.h>

#include <NAAAIM.h>
#include "TTYduct.h"

#include "sancho.h"

/* State extraction macro. */
#define STATE(var) CO(TTYduct_State, var) = this->state

/* Maximum receive buffer size - 1K. */
#define MAX_RECEIVE_SIZE 1024

/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_TTYduct_OBJID)
#error Object identifier not defined.
#endif


/** LocalDuct private state information. */
struct NAAAIM_TTYduct_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;

	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;

	/* End of transmission flag. */
	_Bool eof;

	/* Debug flag. */
	_Bool debug;
	/* Error code. */
	int error;

	/* Output file descriptor. */
	int fd;
};


/** The structure definining the UART is used to implement the object. */
UART_HandleTypeDef Uart;

/* Amount of data to be received. */
static uint32_t Receive_Size;

/* Amount of receiver buffer occupied. */
static uint32_t Input_Size;
static uint8_t Input_Buffer[MAX_RECEIVE_SIZE];

/* Input state. */
static enum {
	receiving_sync=0,
	receiving_size,
	receiving_buffer,
	received_buffer
} Receive_State = receiving_sync;


/* UART receive character location. */
uint8_t NAAAIM_TTYduct_uart_input[MAX_RECEIVE_SIZE];


/* Replacements for byte swapping functions. */
static inline uint32_t htonl(uint32_t value)

{
	return value >> 24 | (value >> 8 & 0xff00) | \
		(value << 8 & 0xff0000) | value << 24;
}

static inline uint32_t ntohl(uint32_t value)

{
	return value >> 24 | (value >> 8 & 0xff00) | \
		(value << 8 & 0xff0000) | value << 24;
}


/**
 * Internal private method.
 *
 * This method is responsible for initializing the NAAAIM_LocalDuct_State
 * structure which holds state information for each instantiated object.
 * The object is started out in poisoned state to catch any attempt
 * to use the object without initializing it.
 *
 * \param S A pointer to the object containing the state information which
 *        is to be initialized.
 */

static void _init_state(CO(TTYduct_State, S)) {

	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_TTYduct_OBJID;


	S->poisoned = false;
	S->debug    = false;
	S->fd	    = fileno(stdout);

	return;
}


/**
 * External public method.
 *
 * This method initializes a local device for communications.
 *
 * \param this	The communications object for which the device is to be
 *		opened.
 *
 * \param path	The path to the device to be used for this object.
 *
 * \return	If the tty device is successfully opened and initialized
 *		a boolean true value is returned.  If open or
 *		initialization fails a false value is returned and the
 *		object is poisoned.
 */

static _Bool init_device(CO(TTYduct, this), CO(char *, path))

{
	bool retn = false;


	/* Clear the input buffer and arm the interrupt handler. */
#if 0
	printf("%s: Called.\r\n", __func__);
	fflush(stdout);
#endif
	Input_Size    = 0;
	memset(Input_Buffer, '\0', sizeof(Input_Buffer));

	Receive_Size  = 0;
	Receive_State = receiving_sync;

	HAL_UART_Receive_IT(&Uart, NAAAIM_TTYduct_uart_input, 1);

	while ( Receive_State == receiving_sync ) {
#if 0
		printf("%s: state=%u\r\n", __func__, Receive_State);
		fflush(stdout);
#endif
		HAL_Delay(5);
	}
#if 0
	printf("%s: Have sync.\r\n", __func__);
	fflush(stdout);
#endif

	retn = true;
	return retn;
}


/**
 * External public method.
 *
 * This method implements accepting a connection on an initialized server
 * port.
 *
 * \param this	The communications object which is to accept a connection.
 *
 * \return	This call blocks until a connection occurs.  The file
 *		descriptor of the connected socket is returned.
 */

static _Bool accept_connection(CO(TTYduct, this))

{
	STATE(S);

	_Bool retn = false;


	/* Verify object status. */
	if ( S->poisoned )
		goto done;

	retn  = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements sending the contents of a specified Buffer object
 * over the connection represented by the callingn object.
 *
 * \param this	The LocalDuct object over which the Buffer is to be sent.
 *
 * \return	A boolean value is used to indicate whether or the
 *		write was successful.  A true value indicates the
 *		transmission was successful.
 */

static _Bool send_Buffer(CO(TTYduct, this), CO(Buffer, bf))

{
	STATE(S);

	_Bool retn = false;

	uint32_t size = htonl(bf->size(bf));

	ssize_t sent;

	Buffer bufr = NULL;


	if ( S->poisoned )
		ERR(goto done);
	if ( (bf == NULL) || bf->poisoned(bf))
		ERR(goto done);


	INIT(HurdLib, Buffer, bufr, ERR(goto done));
	bufr->add(bufr, (unsigned char *) &size, sizeof(size));
	if ( !bufr->add_Buffer(bufr, bf) )
		ERR(goto done);

	if ( S->debug ) {
		fputs("Sending buffer.\n", stdout);
		bufr->hprint(bufr);
		fflush(stdout);
	}

	sent = write(S->fd, bufr->get(bufr), bufr->size(bufr));
	if ( sent != bufr->size(bufr) )
		ERR(S->error = errno; goto done);

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	WHACK(bufr);

	return retn;
}


/**
 * External public method.
 *
 * This method implements loading the specified number of bytes into
 * the provided Buffer object.
 *
 * \param this	The object that data is to be read into.
 *
 * \return	A boolean value is used to indicate whether or the
 *		read was successful.  A true value indicates the receive
 *		was successful.
 */

static _Bool receive_Buffer(CO(TTYduct, this), CO(Buffer, bf))

{
	STATE(S);

	_Bool retn = false;


	/* Validate object. */
	if ( S->poisoned )
		ERR(goto done);
	if ( (bf == NULL) || bf->poisoned(bf) )
		ERR(goto done);


	/* Arm the system for receiving the packet size. */
	Receive_Size  = 0;
	Receive_State = receiving_size;
	HAL_UART_Receive_IT(&Uart, NAAAIM_TTYduct_uart_input, 4);


	/* Block until interrupt handler has a buffer. */
	while ( Receive_State != received_buffer ) {
#if 0
		printf("%s: state=%u, count=%lu, target=%lu\r\n", __func__, \
		       Receive_State, Input_Size, Receive_Size);
		fflush(stdout);
#endif

		HAL_Delay(2);
	}


	/* Load the buffer with the input stream and reset input. */
	if ( !bf->add(bf, Input_Buffer, Receive_Size) )
		ERR(goto done);
	retn = true;


 done:
	Input_Size = 0;
	memset(Input_Buffer, '\0', sizeof(Input_Buffer));

	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method activates debug mode for the object.
 *
 * \param this	A pointer to the object which is to have debug mode
 *		activated.
 */

static void debug(CO(TTYduct, this))

{
	STATE(S);


	S->debug = true;

	return;
}


/**
 * External public method.
 *
 * This method implements a destructor for a LocalDuct object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(TTYduct, this))

{
	STATE(S);


	/* Destroy resources. */
	S->root->whack(S->root, this, S);

	return;
}


/**
 * Internal private function.
 *
 * This function initializes and configures the UART that will be
 * used to support the TTYduct object.
 *
 */
static void uart_init(void)

{
	Uart.Instance			   = USART1;
#if 1
	Uart.Init.BaudRate		   = 115200;
#else
	Uart.Init.BaudRate		   = 9600;
#endif
	Uart.Init.WordLength		   = UART_WORDLENGTH_8B;
	Uart.Init.StopBits		   = UART_STOPBITS_1;
	Uart.Init.Parity		   = UART_PARITY_NONE;
	Uart.Init.Mode			   = UART_MODE_TX_RX;
	Uart.Init.HwFlowCtl		   = UART_HWCONTROL_RTS_CTS;
	Uart.Init.OverSampling		   = UART_OVERSAMPLING_16;
	Uart.Init.OneBitSampling	   = UART_ONE_BIT_SAMPLE_DISABLE;
	Uart.Init.ClockPrescaler	   = UART_PRESCALER_DIV1;
	Uart.AdvancedInit.AdvFeatureInit   = UART_ADVFEATURE_NO_INIT;

	if ( HAL_UART_Init(&Uart) != HAL_OK )
		Error_Handler();

	if ( HAL_UARTEx_SetTxFifoThreshold(&Uart, \
					  UART_TXFIFO_THRESHOLD_1_8) \
	     != HAL_OK)
		Error_Handler();

	if ( HAL_UARTEx_SetRxFifoThreshold(&Uart, \
					   UART_RXFIFO_THRESHOLD_1_8) \
	     != HAL_OK)
		Error_Handler();

	if ( HAL_UARTEx_DisableFifoMode(&Uart) != HAL_OK )
		Error_Handler();

	return;
}


/**
 * External constructor call.
 *
 * This function implements a constructor call for a LocalDuct object.
 *
 * \return	A pointer to the initialized LocalDuct.  A null value
 *		indicates an error was encountered in object generation.
 */

extern TTYduct NAAAIM_TTYduct_Init(void)

{
	Origin root;

	TTYduct this = NULL;

	struct HurdLib_Origin_Retn retn;


	/* Initialize the hardware. */
	uart_init();

	/* Get the root object. */
	root = HurdLib_Origin_Init();

	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_TTYduct);
	retn.state_size   = sizeof(struct NAAAIM_TTYduct_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_TTYduct_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;

	/* Initialize aggregate objects. */

	/* Initialize object state. */
	_init_state(this->state);

	/* Method initialization. */
	this->init_device	= init_device;
	this->accept_connection	= accept_connection;

	this->send_Buffer	= send_Buffer;
	this->receive_Buffer	= receive_Buffer;

	this->debug		= debug;

	this->whack		= whack;

	return this;
}


/**
 * External function call.
 *
 * This function implements the UART interrupt handler that is called
 * for each character that is received.
 *
 * \param uart	A pointer to the structure defining the UART from which
 *		the character is to be received.
 *
 * \return	No return value is defined.
 */

void NAAAIM_TTYduct_interrupt_handler(UART_HandleTypeDef *uart)

{
	size_t request_size = 0;

        static unsigned char sync_char = '@';

	if ( Input_Size >= sizeof(Input_Buffer) )
		goto done;

#if 0
	printf("<%x>", NAAAIM_TTYduct_uart_input);
	fflush(stdout);
#endif

	switch ( Receive_State ) {
		case receiving_sync:
			if ( NAAAIM_TTYduct_uart_input[0] == '\n' ) {
				HAL_UART_Transmit(&Uart, &sync_char, \
						  sizeof(sync_char), 0xffff);
				request_size = 4;
				Receive_State = receiving_size;
				goto done;
			}
			break;

		case receiving_size:
#if 0
		        Input_Buffer[Input_Size] = NAAAIM_TTYduct_uart_input;
			if ( ++Input_Size < sizeof(uint32_t) )
				goto done;
			memcpy(&Receive_Size, Input_Buffer, \
			       sizeof(Receive_Size));
#else
			memcpy(&Receive_Size, NAAAIM_TTYduct_uart_input, \
			       sizeof(Receive_Size));
#endif

			Receive_Size  = ntohl(Receive_Size);
			Receive_State = receiving_buffer;
			request_size  = Receive_Size;

			memset(Input_Buffer, '\0', Input_Size);
			Input_Size = 0;
#if 0
			printf("Waiting for buffer of size=%lu\r\n", \
			       Receive_Size);
			fflush(stdout);
#endif
			break;

		case receiving_buffer:
#if 0
			printf("%lu:<%x/%c> ", Receive_Size, \
			       NAAAIM_TTYduct_uart_input[0], \
			       NAAAIM_TTYduct_uart_input[0]);
			fflush(stdout);
#endif

#if 0
		        Input_Buffer[Input_Size] = NAAAIM_TTYduct_uart_input;
			if ( ++Input_Size == Receive_Size )
				Receive_State = received_buffer;
#else
			memcpy(Input_Buffer, NAAAIM_TTYduct_uart_input, \
			       Receive_Size);
			Receive_State = received_buffer;
#endif

			break;

		case received_buffer:
			break;
	}


 done:
	if ( request_size > 0 )
		HAL_UART_Receive_IT(&Uart, NAAAIM_TTYduct_uart_input, \
				    request_size);
	return;
}


/**
 * External function call.
 *
 * This function vectors the USART1 interrupt vector to the HAL UART
 * interrupt handler.
 *
 * \return	No return value is defined.
 */

void USART1_IRQHandler(void)

{
	HAL_UART_IRQHandler(&Uart);
	return;
}


int __io_putchar(int output)

{
	HAL_UART_Transmit(&Uart, (uint8_t *) &output, 1, 0xFFFF);
	return output;
}


void send_char(uint8_t *output, size_t cnt)

{
	HAL_UART_Transmit(&Uart, output, cnt, 0xFFFF);
	return;
}

void read_char(uint8_t *input, size_t cnt)

{
	if ( HAL_UART_Receive(&Uart, input, cnt, HAL_MAX_DELAY) == \
	     HAL_TIMEOUT ) {
		return;
	}
	return;
}


int __io_getchar(void)

{
	uint8_t inchar;


	if ( HAL_UART_Receive(&Uart, &inchar, 1, HAL_MAX_DELAY) == \
	     HAL_TIMEOUT ) {
		return 0;
	}
	return inchar;
}

