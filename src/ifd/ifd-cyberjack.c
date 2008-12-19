/*____________________________________________________________________________
	Cyberjack reader. 
	
	Tested with USBID 0c4b:0100. These are red readers: one with LCD, 
	another one without. 
	
	Supports PIN-pad authentication. 

	One advantage of this implementaiton is that everything needed to support 
	cyberjack is in this single file, as it is done for other OpenCT reader
	drivers.

	This code doesn't unload cyberjack kernel module, with which it will
	conflict. To do this call "rmmod cyberjack" after the device is plugged in, 
	or better add "blacklist cyberjack" to modprobe.conf.
	
	TODO: cleanup improvised T=1 communication with the reader.

	Written by Andrey Jivsov in 2006. opensc@brainhub.org or ajivsov@pgp.com

	$Id: $
____________________________________________________________________________*/

#include "internal.h"
#include "usb-descriptors.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include "ctbcs.h"

static const unsigned T1_I_SEQ_SHIFT=6;
static const unsigned T1_R_SEQ_SHIFT=4;
static const unsigned DATA_NAD = (0<<4/*card*/) | 2/*host*/;

/* Pseudo-slot referencing the reader. The reader exposes only 
 * one slot corresponding to the card and is at index 0. 
 */
static const int cyberjack_reader_slot = (OPENCT_MAX_SLOTS-1);
static const int cyberjack_card_slot = 0;

struct cyberjack_t1_state {
	unsigned ns;
	ifd_device_t * dev;
	ifd_protocol_t *proto;
	int verify_initiated;
	int verify_timeout;		// in seconds
};

#if !defined(ifd_msleep) && !defined(BG_MODULE)
#define ifd_msleep(x) usleep(x * 1000)
#endif

#if 1
#define cyberjack_ct_error(max_len, fmt, args... ) \
	ct_error( fmt, ##args );
#else
/* define your own here for testing */
#define cyberjack_ct_error(max_len, fmt, args...) \
	ct_error( max_len, fmt, ##args );
#endif

#if 1
#define cyberjack_ifd_debug( level, max_len, args... )  \
	ifd_debug( level, ##args )
#else
/* define your own here for testing */
#define cyberjack_ifd_debug( level, max_len, args... )  \
	ifd_debug( level, max_len, ##args )
#endif

static unsigned int get_checksum( void *p, int len )  {
	int i;
	unsigned char *pp = p;
	unsigned ret=0;
	for( i=0; i<len; i++ )
		ret ^= pp[i];
	return ret;
}

inline static unsigned get_nad_mirrow( unsigned nad )  {
	return (((nad&0xf)<<4) | (nad>>4));
}

static int cyberjack_init_proto( ifd_reader_t *reader, struct cyberjack_t1_state *state )  {
	ifd_protocol_t *p;	
	if( state->proto==NULL )  {
		p = ifd_protocol_new(IFD_PROTOCOL_T1, reader, DATA_NAD);
		if (p == NULL)  {
			cyberjack_ct_error( 40, "cyberjack: internal error: cannot allocate protocol object" );
			return -1;
		}
		state->proto = p;
	}
	else  {
		cyberjack_ct_error( 40, "cyberjack: internal error: protocol already initialized" );
		return -1;
	}
	return 0;
}

static void cyberjack_free_proto( struct cyberjack_t1_state *state )  {
	if( state->proto )
		ifd_protocol_free( state->proto );
}

/*
 * Initialize the device
 */
static int cyberjack_open(ifd_reader_t * reader, const char *device_name)
{
    ifd_device_t *dev;
    struct ifd_usb_device_descriptor de;
	ifd_device_params_t params;
	const int device_name_len = strlen(device_name);
	int ret;

	(void)device_name_len;

	cyberjack_ifd_debug(1, 40+device_name_len, "device=%s", device_name);
	 
	reader->name = "cyberjack reader";
	reader->nslots = cyberjack_card_slot + 1;
	
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) == IFD_DEVICE_TYPE_USB) {
		if (ifd_usb_get_device(dev, &de)) {
			cyberjack_ct_error(80, "cyberjack: device descriptor not found");
			ifd_device_close(dev);
			return -1;
		}
	
		if( de.idProduct == 0x100 )  {
			cyberjack_ct_error(80+device_name_len, "detected e-com/pp at %s, max packet %d\n", 
				device_name, de.bMaxPacketSize0);
			
			params = dev->settings;
			
			// doesn't seem to make the difference:
			params.usb.interface = 0;
			params.usb.altsetting = 0;
			params.usb.configuration = 1;

			cyberjack_ifd_debug(1, 80, "trying to claim interface %d on device, config %d",
				params.usb.interface, params.usb.configuration );
	        
			// for send
		        params.usb.ep_o = 0x02; 
		        params.usb.ep_i = 0x82;
			if (ifd_device_set_parameters(dev, &params) < 0) {
		        cyberjack_ct_error(80+device_name_len, "cyberjack: setting parameters failed. Try /sbin/rmmod cyberjack first");
		        ifd_device_close(dev);
		        return -1;
			}
			cyberjack_ifd_debug(1, 80, "successfully claimed interface" );
  		}
 		else
			ret = -1;
	}
	else {
		cyberjack_ct_error( 80+device_name_len, "cyberjack: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}
    
	ifd_msleep( 200 );
    
	cyberjack_ifd_debug(1, 80, "cyberjack: returning device %p", dev );
	reader->device = dev;
   
	// yes, it is definitely needed to reliably power-up the card
	ifd_device_reset( dev );

	return 0;
}

static int cyberjack_close(ifd_reader_t *reader)  {
	free( reader->driver_data );
	reader->driver_data = NULL;
	return 0;
}

 // everything is fine, except first byte of T=1 indicates wrong sender or recipient					
#define CJ_RCV_OTHER -1


// convenience function to send T=1 datagram to the reader
// t1_data_no_checksum is nad pcb len [body], i.e. T=1 datagram without the checksum
// so we can calculate it depending on ns
static int cyberjack_send_t1( struct cyberjack_t1_state *state, char *t1_data, int send_len )  {
	unsigned char send_buffer[64];
	int ret;
	if( send_len > sizeof(send_buffer)-3 )
		return -1;
	memcpy( send_buffer+3, t1_data, send_len );
	send_len++;	// for checksum
	send_buffer[0] = 0;
	send_buffer[1] = send_len & 0xff;
	send_buffer[2] = send_len >> 8;
	send_buffer[3+1] |= (state->ns << T1_I_SEQ_SHIFT);		// set the toggle
	send_buffer[3+send_len-1] = get_checksum( send_buffer+3, send_len-1 );
	ret = ifd_device_send( state->dev, send_buffer, send_len+3 );
	if( ret < 0 )
		return ret;
	return ret-4;
}

/* send special S-Block reply */
static int cyberjack_resync_t1(struct cyberjack_t1_state *state, unsigned char nad, unsigned cmd)  {
	ifd_device_t * const dev = state->dev;
	unsigned char send_buffer[64];
	int ret;
	
	send_buffer[0] = 0;
	send_buffer[1] = 4;
	send_buffer[2] = 0;
	
	send_buffer[3] = get_nad_mirrow(nad);	// NAD for device communication
	send_buffer[4] = cmd;
	send_buffer[5] = 0;
	send_buffer[6] = get_checksum(send_buffer+3, 3);
		
	ret = ifd_device_send( dev, send_buffer, 7 );
	if( ret > 0 )
		state->ns = 0;
		
	return ret;
}

/* doesn't seem to make the difference which values I put in timeouts, 
 * but the reply itself is necessary 
 */
static int cyberjack_extend_t1(struct cyberjack_t1_state *state, const unsigned char *t1_in)  {
	ifd_device_t * const dev = state->dev;
	unsigned char send_buffer[64];
	int len;

	send_buffer[0] = 0;
	send_buffer[2] = 0;
	
	send_buffer[3] = get_nad_mirrow(t1_in[0]);
	if( t1_in[2]==0 )  {
		send_buffer[1] = 4;

		send_buffer[4] = 0xe3;
		send_buffer[5] = 0;
		send_buffer[6] = get_checksum(send_buffer+3, 3);
		len = 3+4;
	}
	else  {
		send_buffer[1] = 5;

		send_buffer[4] = 0xe3;
		send_buffer[5] = 1;
		send_buffer[6] = t1_in[3];
		send_buffer[7] = get_checksum(send_buffer+3, 4);
		len = 3+5;
	}
	
	return ifd_device_send( dev, send_buffer, len );
}

// the nad os for the previous send command
static int cyberjack_recv_t1( struct cyberjack_t1_state *state, unsigned nad, unsigned char t1_out[64])  {
	unsigned char read_buffer[64];
	ifd_device_t * const dev = state->dev;
	int ret=-1;
	
start:
	if( (ret=ifd_device_recv( dev, read_buffer, 64, 8000 )) < 7 )  {
		cyberjack_ct_error(80, "cyberjack: failed to activate 2");
		return -1;
	}
	cyberjack_ifd_debug(1, 80+ret*3, "cyberjack: response %s", ct_hexdump( read_buffer, ret ));

	if( read_buffer[0]!=0 || read_buffer[1]!=ret-3 || read_buffer[2]!=0 )  {
		cyberjack_ifd_debug(1, 80, "cyberjack: wrong header");
		return CJ_RCV_OTHER;
	}
	
	if( get_checksum( read_buffer+3, ret-3 ) )  {
		cyberjack_ifd_debug(1, 80, "cyberjack: checksum mismatch");
		return CJ_RCV_OTHER;
	}
	
	ret -= 3;
	
	memcpy( t1_out, read_buffer+3, ret );
	cyberjack_ifd_debug(1, 80+ret*3, "cyberjack: returning %s", ct_hexdump( t1_out, ret ));

	if( (t1_out[1]&0xC0)==0x80 ) {
		cyberjack_ifd_debug(1, 40, "R-BLOCK");			
		if( ((t1_out[1] >> T1_R_SEQ_SHIFT) & 1) != state->ns )  {
				state->ns ^= 1;
				cyberjack_ifd_debug(1, 80, "*** cyberjack: switching ns to %d", state->ns); 
			}
	}
	if( (t1_out[1]&0x80)==0x00 ) {
		cyberjack_ifd_debug(1, 40, "I-BLOCK");
		state->ns ^= 1;
		cyberjack_ifd_debug(1, 80, "*** cyberjack: switching ns to %d", state->ns); 
	}
	// S-BLOCK is (t1_out[1]&0xC0)==0xC0
	switch( t1_out[1] ) {
		case 0xc1:
			cyberjack_ifd_debug(1, 40, "S-BLOCK IFD request");
			break;
		case 0xc2:
			cyberjack_ifd_debug(1, 40, "S-BLOCK Abort request");
			break;
		case 0xc3:
			cyberjack_ifd_debug(1, 40, "S-BLOCK WTX request");
			
			ret = cyberjack_extend_t1( state, t1_out );
			if( ret < 0 )
				return ret;
			goto start;
			
			break;
		case 0xe0:
			cyberjack_ifd_debug(1, 40, "S-BLOCK Resync response 2");
			break;
		case 0xc0:
			cyberjack_ifd_debug(1, 40, "S-BLOCK Resync request");
			break;
		case 0xc4:
		case 0xf4:
			cyberjack_ifd_debug(1, 40, "S-BLOCK key pressed request");
			// reply
			ret = cyberjack_send_t1( state, "\xe2\xe4\x00", 3 );
			if( ret < 0 )
				return ret;
			goto start;

			break;
		case 0xe6:
		case 0xf6:
			cyberjack_ifd_debug(1, 40, "S-BLOCK to throw away");
			break;
		case 0xe5:
		case 0xf5:
			cyberjack_ifd_debug(1, 40, "S-BLOCK card (not) present");

			ret = cyberjack_resync_t1( state, t1_out[0], 0xd5 );
			if( ret < 0 )
				return ret;
			goto start;
			
			break;
		default:
			if( (t1_out[1]&0xC0)==0xC0 )  {
				cyberjack_ifd_debug(1, 40, "unknown S-BLOCK");
			}
			break;
	}

	return ret;
}

/*
 * Power up the reader
 */
static int cyberjack_activate(ifd_reader_t *reader)
{
    unsigned char read_buffer[64];
    ifd_device_t * const dev = reader->device;
  	int ret;
	int result = -1;
  	struct cyberjack_t1_state *state = NULL;
  
	cyberjack_ifd_debug(1, 40, "called (dev = 0x%x).", reader->device);

	if( reader->driver_data )  {
		cyberjack_free_proto( reader->driver_data );
		free( reader->driver_data );
		reader->driver_data = NULL;
	}

	state = calloc( 1, sizeof(struct cyberjack_t1_state) );
	if( state == NULL )
		goto cleanup;
		
	state->dev = dev;
	
	if( ifd_device_send( dev, (unsigned char*)"\x00\x04\x00" "\xe2\xc1\x00\x23", 7 ) != 7 ||
	    ifd_device_send( dev, (unsigned char*)"\x00\x04\x00" "\xe2\xc0\x00\x22", 7 ) != 7 )
	{
		cyberjack_ct_error(80, "cyberjack: failed to activate 1");
		goto cleanup;
	}
	
	ifd_msleep( 100 );
	if( (ret=cyberjack_recv_t1( state, 0xe2, read_buffer ))!=4 || memcmp( read_buffer, "\x2e\xe0\x00\xce", 4 )!=0 )  {
		cyberjack_ct_error(80, "cyberjack: failed to activate 2: no cookie");
		goto cleanup;
	}

	// send 20 11 00 00 
	if( cyberjack_send_t1( state, "\x12\x00\x04" "\x20\x11\x00\x00", 7 ) != 7 )  {
		cyberjack_ct_error(80, "cyberjack: failed to activate 5");		
		goto cleanup;
	}
	ret = cyberjack_recv_t1( state, 0x12, read_buffer );
	if( ret < 0 )  {
		cyberjack_ct_error(80, "cyberjack: failed to activate 5.1");		
		goto cleanup;
	}
	cyberjack_ifd_debug(1, 80+ret*3, "cyberjack: t1 response is : %s", 
			ct_hexdump( read_buffer, ret ));
	if( ret < 6 )  {
		cyberjack_ifd_debug(1, 80, "cyberjack: response is short 6.1");
		goto cleanup;
	}
	if( ret != 6 || read_buffer[3]!=0x90 || read_buffer[4]!=0 )
	{
		cyberjack_ifd_debug(1, 80, "cyberjack: response to 20 11 00 00:  %s", 
			ct_hexdump( read_buffer, 6 ));
		// could neever recover from this
		cyberjack_ct_error(80, "cyberjack: failed to activate: failed to reset the reader");		
		goto cleanup;
	}

	// The following is needed to transition from our protocol to 
	// CT's protocol object. What we want here is state->ns to turn 0
	// so CT's T=1 protocol object can take over from here
	ret = cyberjack_resync_t1( state, 0x2e, 0xc0/*request to resync*/ );
	if( ret < 0 )  {
		cyberjack_ct_error(80, "cyberjack: failed to activate in resync");		
	}
	ret = cyberjack_recv_t1( state, 0x2e, read_buffer );
	if( ret < 3 || memcmp(read_buffer, "\x2e\xe0\x00", 3 )!=0 )  {
		cyberjack_ct_error(80, "cyberjack: failed to activate 7.1");		
		goto cleanup;
	}
	
	reader->driver_data = state;
	state = NULL;
	
	cyberjack_init_proto( reader, (struct cyberjack_t1_state *)reader->driver_data );
	
	cyberjack_ifd_debug(1, 80, "cyberjack: activated OK, ns=%d",
		((struct cyberjack_t1_state *)reader->driver_data)->ns);
	
	result = 0;

cleanup:
	if (state != NULL) {
		free(state);
	}

	return result;
}

static int cyberjack_deactivate(ifd_reader_t * reader)
{
	struct cyberjack_t1_state *state = reader->driver_data;

	cyberjack_ifd_debug(1, 40, "called.");

	cyberjack_free_proto( state );
	free( state );
	reader->driver_data = NULL;

    /* if there are some cards that are powered on, power them off */

	return 0;
}

/*
 * Card status - always present
 */
static int cyberjack_card_status(ifd_reader_t * reader, int slot, int *status)
{
	struct cyberjack_t1_state *state = reader->driver_data;
	unsigned char response[64];
	ifd_slot_t *pslot;
	ifd_protocol_t *proto;
	int ret;
	unsigned stat;
	
	cyberjack_ifd_debug(1, 40, "slot=%d", slot);
	
	if( state==NULL || slot != cyberjack_card_slot )
		return -1;

	pslot = &reader->slot[slot];

	if( pslot->proto )
		proto = pslot->proto;
	else
		proto = state->proto;

	// cyberjack_ifd_debug(1, 80, "proto=%p, pslot=%p, state=%p", proto, pslot, state);
		
	ret = ifd_protocol_transceive( proto, 0x12, "\x20\x13\x00\x80\x00", 5, 
		response, sizeof(response) );
	if( ret < 0 )  {
		cyberjack_ct_error(80, "cyberjack: failed to get status");		
		return ret;
	}
	
	cyberjack_ifd_debug(1, 80+ret*3, "cyberjack: response to get status: %s", 
		ct_hexdump( response, ret ));
		
	// saw this: 80 01 03 90 00 for inserted card
	//           80 01 00 90 00 for removed
	stat = (( ret > 3 && response[ret-2]==0x90 && response[ret-1]==0 && response[ret-3]!=0 ) ? IFD_CARD_PRESENT : 0);

	if( !(stat & IFD_CARD_PRESENT) && pslot->proto )  {
		cyberjack_ifd_debug(1, 80, "cyberjack: card removed");
		/* hide protocol object from openct, or we will not be able to reset
		 * re-inserted card. 
		 */
		state->proto = pslot->proto;
		pslot->proto = NULL;
		stat |= IFD_CARD_STATUS_CHANGED;
	}

	*status = stat;
	return 0;
}

/* this is when the light on the reader goes on */
static int cyberjack_card_reset(ifd_reader_t * reader, int slot, void *atr,
							 size_t size)
{
	unsigned char response[64];
	struct cyberjack_t1_state * const state = (struct cyberjack_t1_state *)reader->driver_data;
	ifd_slot_t *pslot; 
	ifd_protocol_t *proto;
	int ret;

	cyberjack_ifd_debug(1, 40, "called.");

	if( slot != cyberjack_card_slot )
		return -1;
	
	pslot = &reader->slot[slot];

	if( pslot->proto )
		proto = pslot->proto;
	else
		proto = state->proto;

	if( proto==NULL )  {
		cyberjack_ifd_debug(1, 40, "cannot obtain protocol object, slot=%d", slot);
		return -1;
	}

	// 0x14 here is timeout in secs.
	ret = ifd_protocol_transceive( proto, 0x12, "\x20\x12\x01\x01\x01\x14\x00", 7, 
		response, sizeof(response) );
	if( ret < 0 )  {
		cyberjack_ct_error(80, "cyberjack: failed to get ATR: err=%d", ret);		
		return ret;
	}
	
	cyberjack_ifd_debug(1, 80+ret*3, "cyberjack: response to get ATR: %s", 
		ct_hexdump( response, ret ));

	memcpy( atr, response, ret );

	return ret;
}

static int cyberjack_set_protocol(ifd_reader_t * reader, int slot, int proto)
{
	ifd_slot_t *pslot;
	struct cyberjack_t1_state * const state = (struct cyberjack_t1_state *)reader->driver_data;

	cyberjack_ifd_debug(1, 40, "slot=%d", slot);

	if( slot != cyberjack_card_slot )
		return -1;

	if (proto != IFD_PROTOCOL_T1 && proto != IFD_PROTOCOL_T0 ) {
		cyberjack_ct_error(80+strlen(reader->name), "%s: protocol %d not supported", reader->name, proto);
		return IFD_ERROR_NOT_SUPPORTED;
	}
	pslot = &reader->slot[slot];
	
	// this is actually used as (dad<<4 | sad) inside
	// the reply is never checked and is expected to be the 4 bit flipped version
	pslot->dad = DATA_NAD;

	/* Protocol is already allocated. Detach it from the state and attach to the slot
	 * for CT to use it. */
	if( state->proto == NULL )  {	
		cyberjack_ct_error( 40+strlen(reader->name), "%s: internal error", reader->name);
		return IFD_ERROR_GENERIC;
	}
	if (pslot->proto) {
		ifd_protocol_free(pslot->proto);
		pslot->proto = NULL;
	}
	pslot->proto = state->proto;
	state->proto = NULL;

	// To simulate communication stall (not USB stall) comment these out. 
	// You will get S-BLOCK T=1 response that the core cannot handle... 
	ifd_protocol_set_parameter(pslot->proto, IFD_PROTOCOL_T1_IFSD, 254);
	cyberjack_ifd_debug(1, 80, "set protocol's IFSd size to %d", 254);
	ifd_protocol_set_parameter(pslot->proto, IFD_PROTOCOL_T1_IFSC, 254);
	cyberjack_ifd_debug(1, 80, "set protocol's IFSc size to %d",254);

	return 0;
}


static int cyberjack_send(ifd_reader_t *reader, unsigned int dad,
		     const unsigned char *buffer, size_t len)
{
	unsigned char request[512];
    ifd_device_t * const dev = reader->device;
	int ret;
	
	cyberjack_ifd_debug(1, 40, "called with dad=%02x, len=%d", dad, len);	
	
	if( len > sizeof(request)-3 )  {
		cyberjack_ct_error(80+strlen(reader->name), "%s: request length too large: %d", reader->name, len);
		return -1;
	}
	
	request[0] = 0;
	request[1] = len & 0xff;
	request[2] = len >> 8;
 
	len+=3;

	memcpy( request+3, buffer, len );

	ret = ifd_device_send( dev, request, len );

	return ret;
}

/* TODO: working on generatlization of time with ifd_time, ifd_get_current_time, ifd_time_elapsed2 */
struct ifd_time  {
	unsigned p1;
	unsigned p2;	/* These must be opaque */
};
/* a wrapper so both are in one place */
static void ifd_get_current_time( struct ifd_time *now )  {
	struct timeval now_tv;

	gettimeofday(&now_tv, NULL);

	now->p1 = now_tv.tv_sec;
	now->p2 = now_tv.tv_usec;
}
static long ifd_time_elapsed2( struct ifd_time *then )
{
	struct timeval now, delta, then_tv;
	long l;

//ct_debug(40, "ifd_time_elapsed2");
	then_tv.tv_sec = then->p1;
	then_tv.tv_usec = then->p2;

	gettimeofday(&now, NULL);
	timersub(&now, &then_tv, &delta);
	l = delta.tv_sec * 1000 + (delta.tv_usec / 1000);
//ct_debug(40, "ifd_time_elapsed2 returns %ld", l);
	return l;
}

/* TODO: we need to loop here to make sure we are buffering the whole response 
 * before parsing it. */
static int cyberjack_recv(ifd_reader_t *reader, unsigned int dad,
		     unsigned char *buffer, size_t len, long timeout)
{
    unsigned char response[512];
	unsigned response_size=0;
    ifd_device_t * const dev = reader->device;
	struct cyberjack_t1_state * const state = (struct cyberjack_t1_state *)reader->driver_data;
	int tries;
	int ret;
	struct ifd_time time_start;
	long deadline;

	cyberjack_ifd_debug(1, 40, "called with dad=%02x, len=%d", dad, len);	

	if( len > sizeof(response)-3 )  {
		cyberjack_ct_error(80, "cyberjack: response length too large: %d", len);
		return -1;
	}

	/* some upper bound number that we hope we will not hit: 
	 * more for operations involving user interaction 
	 */
	tries = (state->verify_initiated ? 100 : 20);
	
	ifd_get_current_time( &time_start );

	/* in ms; 110% of requested time to let the reader fail instead of us
	 */
	deadline = state->verify_timeout*1100;
	
	while( tries-- )  {
		response_size = len+3;
		
		if( state->verify_initiated && ifd_time_elapsed2( &time_start ) > deadline )  {
			cyberjack_ct_error(80, "cyberjack: cannot complete verify operation in %d seconds", 
				deadline/1000 );
			return IFD_ERROR_TIMEOUT;
		}
	
		ret = ifd_device_recv( dev, response, response_size, timeout );
		if( ret < 0 )
			return ret;
	
	//	cyberjack_ifd_debug(1, 80, "cyberjack: response %s", ct_hexdump( response, ret ) );
	
		if( ret < 3+4 || response[0] != 0 ) {
			cyberjack_ct_error(80, "cyberjack: response %s is too short", ct_hexdump( response, ret ) );
			break; // pass through // return IFD_ERROR_GENERIC;
		}
	
		response_size = response[1] | (response[2]<<8);
		if( response_size != ret-3 )	{
			cyberjack_ifd_debug(1, 80+ret*3, "cyberjack: inconsistent length in response %s", ct_hexdump( response, ret ));
			break;	// pass through
		}
	
		/* This is urgly. We need to watch for a few proprietary S-BLOCKS that 
		 * core CT protocol handler cannot handle. Fortunately, we don't neet to 
		 * maintain the nabble for these. */
		switch( response[3+1] )  {
		case 0xf4:	// key pressed (OK key)
		case 0xc4:	// key pressed (digit keys)
			cyberjack_ifd_debug(1, 80, "cyberjack: key pressed");
			ret = cyberjack_send_t1( state, "\xe2\xe4\x00", 3 );
			if( ret < 0 )
				return ret;
			continue;	/* re-read again */
		case 0xc3:	// WTX
			cyberjack_ifd_debug(1, 80, "timeout, grant extension, %d tries remains", tries);		
			ret = cyberjack_extend_t1( state, response+3 );
			if( ret < 0 )
					return ret;
			continue;	/* re-read again */
		}
		
		break;	// pass through
	}
	
	memcpy( buffer, response+3, response_size );
	
	return response_size;
}

/*
 * Perform a PIN verification.
 * Timeout in seconds.
 */
static int cyberjack_perform_verify(ifd_reader_t * reader, int slot,
			       unsigned int timeout, const char *prompt,
			       const unsigned char *data, size_t data_len,
			       unsigned char *resp, size_t resp_len)
{
	unsigned char buffer[256];
	unsigned short sw;
	ifd_slot_t *pslot;
	struct cyberjack_t1_state * const state = (struct cyberjack_t1_state *)reader->driver_data;
	int err;

	if( slot != cyberjack_card_slot || state==NULL )
		return -1;

	pslot = &reader->slot[slot];
	
	cyberjack_ifd_debug(1, 40, "cyberjack: perform_verify timeout=%d", timeout);		

	if( timeout==0 )
		timeout = 30;

#if 1
	err = ctbcs_build_perform_verify_apdu(buffer, sizeof(buffer),
					    slot + 1, prompt, timeout,
					    data, data_len);
	if (err < 0)
		return err;
#else
	{
		ct_buf_t buf;

		ct_buf_init(&buf, buffer, sizeof(buffer));
		ctbcs_begin(&buf, 0x18, slot+1, 0x00);

		ct_buf_putc(&buf, 0x52);
		ct_buf_putc(&buf, data_len);
		ct_buf_put(&buf, data, data_len);

		ctbcs_add_timeout(&buf, 0x80 /*timeout*/);

		if (ct_buf_overrun(&buf))
			return IFD_ERROR_BUFFER_TOO_SMALL;

		buffer[4] = ct_buf_avail(&buf) - 5;	/* lc */
		err = ct_buf_avail(&buf);
	}
#endif

	state->verify_initiated = 1;
	state->verify_timeout = timeout;

	/* fetch the protocol object directly because we want to use another NAD */
	err = ifd_protocol_transceive(pslot->proto, 0x12, buffer, err, resp, resp_len);

	state->verify_initiated = 0;

	if (err < 0) {
		cyberjack_ct_error(80, "perform_verify failed with err=%d", err);
		return err;
	}
	
	if( err < 2 )
		return IFD_ERROR_COMM_ERROR;
	sw = (resp[err-2] << 8) | resp[err-1];
	
	cyberjack_ct_error(80, "perform_verify: err=%d sw=%04x", err, (unsigned)sw);	

	if( sw >= 0x6300 && sw < 0x63cf )
		sw &= 0xff00;

	ifd_msleep(500);

	switch (sw) {
	case 0x6400:
		cyberjack_ct_error(80, "perform_verify failed: timeout");
		return IFD_ERROR_USER_TIMEOUT;
	case 0x6401:
		cyberjack_ct_error(80, "perform_verify failed: user pressed cancel");
		return IFD_ERROR_USER_ABORT;
	case 0x6300:
		cyberjack_ct_error(80, "perform_verify failed: PIN mismatch");
		return IFD_ERROR_PIN_MISMATCH;
	}

	return 2;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops cyberjack_driver;

/*
 * Initialize this module
 */
void ifd_cyberjack_register(void)
{
	cyberjack_driver.open = cyberjack_open;
	cyberjack_driver.activate = cyberjack_activate;
	cyberjack_driver.deactivate = cyberjack_deactivate;
	
	cyberjack_driver.card_status = cyberjack_card_status;
	cyberjack_driver.card_reset = cyberjack_card_reset;
	cyberjack_driver.set_protocol = cyberjack_set_protocol;
	cyberjack_driver.send = cyberjack_send;
	cyberjack_driver.recv = cyberjack_recv;
	cyberjack_driver.close = cyberjack_close;
	cyberjack_driver.perform_verify = cyberjack_perform_verify;

	ifd_driver_register("cyberjack", &cyberjack_driver);
}
