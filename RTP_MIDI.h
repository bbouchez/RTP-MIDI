/*
 *  RTP_MIDI.h
 *  Cross-platform RTP-MIDI session initiator/listener endpoint class
 *
 *  Copyright (c) 2012/2024 Benoit BOUCHEZ (BEB)
 *
 * License : MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

//---------------------------------------------------------------------------
#ifndef __RTP_MIDI_H__
#define __RTP_MIDI_H__
//---------------------------------------------------------------------------

#include "network.h"

#define LONG_B_BIT 0x8000
#define LONG_J_BIT 0x4000
#define LONG_Z_BIT 0x2000
#define LONG_P_BIT 0x1000

#define SHORT_J_BIT 0x40
#define SHORT_Z_BIT 0x20
#define SHORT_P_BIT 0x10

#define MAX_SESSION_NAME_LEN	64

// Max size for one RTP payload
#define MAX_RTP_LOAD 1024
// Max size for a single fragmented SYSEX
#define SYSEX_FRAGMENT_SIZE		512

#define DEFAULT_RTP_ADDRESS 0xC0A800FD
#define DEFAULT_RTP_DATA_PORT 5004
#define DEFAULT_RTP_CTRL_PORT 5003

// Session status
#define SESSION_CLOSED			0	// No action
#define SESSION_CLOSE			1	// Session should close in emergency
#define SESSION_INVITE_CONTROL	2	// Sending invitation on control port
#define SESSION_INVITE_DATA		3	// Sending invitation on data port
#define SESSION_CLOCK_SYNC0		5	// Send first synchro message and wait answer (CK0)
#define SESSION_CLOCK_SYNC1		6   // Wait for CK1 message from remote node
#define SESSION_CLOCK_SYNC2		7	// Send second synchro message (CK2)
#define SESSION_OPENED			8	// Session is opened, just generate background traffic now

#define SESSION_WAIT_INVITE_CTRL		10	// Wait to be invited by remote station on control port
#define SESSION_WAIT_INVITE_DATA		11	// Wait to be invited by remote station on data port
#define SESSION_WAIT_CLOCK_SYNC			12  // Wait to receive CK2 message to confirm session is fully opened by remote initiator

#pragma pack (push, 1)
typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int ProtocolVersion;
  unsigned int InitiatorToken;
  unsigned int SSRC;
  unsigned char Name [1024];
} TSessionPacket;

typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int ProtocolVersion;
  unsigned int InitiatorToken;
  unsigned int SSRC;
} TSessionPacketNoName;

typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int SSRC;
  unsigned char Count;
  unsigned char Unused [3];       // 0
  unsigned int TS1H;    // Timestamp 1
  unsigned int TS1L;
  unsigned int TS2H;    // Timestamp 2
  unsigned int TS2L;
  unsigned int TS3H;    // Timestamp 3
  unsigned int TS3L;
} TSyncPacket;

typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int SSRC;
  unsigned short SequenceNumber;
  unsigned short Unused;
} TFeedbackPacket;

typedef struct {
  unsigned char Control;     // B/J/Z/P/Len(4)
                        // 0 : header = 1 byte
                        // 0 : no journalling section / 1 : journalling section after MIDI list
                        // 0 : no deltatime for first MIDI command / 1 : deltatime before MIDI command
                        // Phantom status
                        // MIDI list length = 15 bytes max
  unsigned char MIDIList [15];
} TShortMIDIPayload;

typedef struct {
  unsigned short Control;             // B/J/Z/P/Len(12)
                                // 1 : header = 2 bytes
                                // 0 : no journalling section / 1 : journalling section after MIDI list
                                // 0 : no deltatime for first MIDI command / 1 : deltatime before MIDI command
                                // Phantom status
                                // MIDI list length = 4095 bytes max
  unsigned char MIDIList[MAX_RTP_LOAD];
} TLongMIDIPayload;

typedef struct {
  // Header RTP
  unsigned char Code1;
  unsigned char Code2;
  unsigned short SequenceNumber;
  unsigned int Timestamp;
  unsigned int SSRC;
} TRTP_Header;

typedef struct {
  TRTP_Header Header;
  TLongMIDIPayload Payload;
} TLongMIDIRTPMsg;

typedef struct {
  TRTP_Header Header;
  TShortMIDIPayload Payload;
} TShortMIDIRTPMsg;
#pragma pack (pop)

#define MIDI_CHAR_FIFO_SIZE		2048

typedef struct {
	unsigned char FIFO[MIDI_CHAR_FIFO_SIZE];
	unsigned int ReadPtr;
	unsigned int WritePtr;
} TMIDI_FIFO_CHAR;

#ifdef __TARGET_MAC__
// This callback is called from realtime thread. Processing time in the callback shall be kept to a minimum
typedef void (*TRTPMIDIDataCallback) (void* UserInstance, unsigned int DataSize, unsigned char* DataBlock, unsigned int DeltaTime);
#endif

#ifdef __TARGET_LINUX__
typedef void (*TRTPMIDIDataCallback) (void* UserInstance, unsigned int DataSize, unsigned char* DataBlock, unsigned int DeltaTime);
#endif

#ifdef __TARGET_WIN__
typedef void (CALLBACK *TRTPMIDIDataCallback) (void* UserInstance, unsigned int DataSize, unsigned char* DataBlock, unsigned int DeltaTime);
#endif


class CRTP_MIDI
{
public:
    unsigned int LocalClock;           // Timestamp counter following session initiator

    //! \param SYXInSize size of incoming SYSEX buffer (maximum size of input SYSEX message to be returned to application)
    //! \param CallbackFunc pointer a function which will be called each time a packet is received from RTP-MIDI. Set 0 to disable callback
    //! \param UserInstance value which will be passed in the callback function
	CRTP_MIDI(unsigned int SYXInSize,
			  TRTPMIDIDataCallback CallbackFunc,
			  void* UserInstance);
	~CRTP_MIDI(void);

	//! Record a session name. Shall be called before InitiateSession.
	void setSessionName (char* Name);

	//! Activate network resources and starts communication (tries to open session) with remote node
	// \return 0=session being initiated -1=can not create control socket -2=can not create data socket
	int InitiateSession(unsigned int DestIP,
						unsigned short DestCtrlPort,
						unsigned short DestDataPort,
						unsigned short LocalCtrlPort,
						unsigned short LocalDataPort,
						bool IsInitiator);
	void CloseSession(void);

	//! Main processing function to call from high priority thread (audio or multimedia timer) every millisecond
	void RunSession(void);

	//! Send a RTP-MIDI block (with leading delta-times)
	bool SendRTPMIDIBlock (unsigned int BlockSize, unsigned char* MIDIData);

	//! Returns the session status
	/*!
	 0 : session is closed
	 1 : inviting remote node
	 2 : synchronization in progress
	 3 : session opened (MIDI data can be exchanged)
	 */
	int getSessionStatus (void);

	//! Returns measured latency (0xFFFFFFFF means latency not available) in 1/10 ms
	unsigned int GetLatency (void);

	//! Restarts session process after it has been closed by a remote partner
	// This method is allowed only if RTP-MIDI handler has been declared as session initiator
	void RestartSession (void);

	//! Returns true if remote device does not reply anymore to sync / keepalive messages
	//! The flag is reset after this method has been called (so the method returns true only one time when event has occured)
	bool ReadAndResetConnectionLost (void);

	//! Returns true if remote participant has sent a BY to close the session
	//! The flag is reset after this method has been called (so the method returns true only one time)
	bool RemotePeerHasClosedSession (void);

	//! Returns true if remote participant has rejected the invitation to session
	//! The flag is reset after this method has been called (so the method returns true only one time)
	bool RemotePeerHasRefusedSession(void);

	//! Declares callback and instance parameter for the callback
	void SetCallback (TRTPMIDIDataCallback CallbackFunc, void* UserInstance);

private:
	// Callback data
	TRTPMIDIDataCallback RTPCallback;	// Callback for incoming RTP-MIDI message
	void* ClientInstance;

	unsigned char SessionName [MAX_SESSION_NAME_LEN];

	unsigned int RemoteIPToInvite;				// Address of remote computer (0 if module is used as session listener)
	unsigned int SessionPartnerIP;              // IP address of session partner
	unsigned short PartnerControlPort;			// Remote control port number (0 if module is used as session listener)
	unsigned short PartnerDataPort;				// Remote data port number (0 if module is used as session listener)

	TSOCKTYPE ControlSocket;
	TSOCKTYPE DataSocket;

	bool SocketLocked;
	unsigned int SSRC;
	unsigned short RTPSequence;
	unsigned short LastRTPCounter;		// Last packet counter received from session partner
	unsigned short LastFeedbackCounter;	// Last packet counter sent back in the RS packet
	int SessionState;
	unsigned int InviteCount;		// Number of invitation messages sent
	unsigned int InitiatorToken;
	bool IsInitiatorNode;
	int TimeOutRemote;				// Counter to detect loss of remote node (reset when CK2 is received)
	unsigned int SyncSequenceCounter;		// Count how may sync sequences have been sent after invitation

	unsigned int MeasuredLatency;

	bool TimerRunning;				// Event timer is running
	unsigned int EventTime;			// Time to which event will be signalled

	unsigned int TimeCounter;		// Counter in 100us used for clock synchronization

	TMIDI_FIFO_CHAR RTPStreamQueue;		// Streaming MIDI messages with precomputed RTP deltatime

	// Decoding variables for incoming RTP message
	bool SYSEX_RTPActif;			// We are receiving a SYSEX message from network
	unsigned char FullInMidiMsg[3];
	bool IncomingThirdByte;
	unsigned char RTPRunningStatus;	// Running status from network to client

	// Members to decode SYSEX data coming from network
	unsigned int InSYSEXBufferSize;		// Size of SYSEX defragmentation buffer
	bool SegmentSYSEXInput;				// SYSEX message is segmented across multiple RTP messages
	unsigned char* InSYSEXBuffer;
	unsigned int InSYSEXBufferPtr;		// Number of SYSEX bytes received
	bool InSYSEXOverflow;				// Received SYSEX message can not fit in the local buffer

	unsigned int TS1H;
	unsigned int TS1L;
	unsigned int TS2H;
	unsigned int TS2L;
	unsigned int TS3H;
	unsigned int TS3L;

	bool ConnectionLost;				// Set to 1 when connection is lost after a session has opened successfully
	bool PeerClosedSession;				// Set to 1 when we receive a BY message on a opened session
	bool ConnectionRefused;				// Set to 1 when remote device refuses the invitation

	void CloseSockets(void);
	void SendInvitation (bool DestControl);

	//! Sends an answer to an invitation
	//! \param Accept true : send invitation accepted message, false : send invitation rejected message
	void SendInvitationReply (bool FromControlSocket, bool Accept, unsigned int DestinationIP, unsigned short DestinationPort);

	void SendSyncPacket (char Count, unsigned int TS1H, unsigned int TS1L, unsigned int TS2H, unsigned int TS2L, unsigned int TS3H, unsigned int TS3L);
	void SendBYCommand (void);

	// Sends a RS packet (synchronization/flush of RTP journal)
	void SendFeedbackPacket (unsigned short LastNumber);

	void PrepareTimerEvent (unsigned int TimeToWait);

	//! Extracts and return delta time stored in network buffer
	/*!
	 \param : BufPtr = pointer sur octets a lire dans le tampon RTP
	 \param : ByteCtr = number of byte read (updated by function). Must contain the position of first byte to read at call
	 */
	unsigned int GetDeltaTime(unsigned char* BufPtr, int* ByteCtr);

	//! Fill the payload area of RTP buffer with MIDI data to send to the network
	//! \return Number of bytes put in payload (0 = no data to be sent)
	int GeneratePayload (unsigned char* MIDIList);

	//! Prepare a RTP_MIDI for sending on the network
	//* Returns the size of generated message. Value 0 means no MIDI data to send */
	int PrepareMessage (TLongMIDIRTPMsg* Buffer, unsigned int TimeStamp);

	//! Analyze incoming RTP frame from network
	/*! Buffer = buffer containing RTP message received */
	void ProcessIncomingRTP (unsigned char* Buffer);

	//! Read and decode next MIDI event in RTP reception buffer and send it to callback
	void GenerateMIDIEvent(unsigned char* Buffer, int* ByteCtr, int TailleBloc, unsigned int DeltaTime);

	//! Initializes local SYSEX buffer
	void initRTP_SYSEXBuffer(void);

	//! Store a byte in the SYSEX buffer
	void storeRTP_SYSEXData (unsigned char SysexData);

	//! Send the SYSEX buffer to client
	void sendRTP_SYSEXBuffer (unsigned int DeltaTime);

	//! Send the MIDI message to client (max 3 bytes)
	void sendMIDIToClient (unsigned int NumBytes, unsigned int DeltaTime);
	
	//! Process communication on Control socket (processing of incoming invitations)
	//! \return true if a packet has been received on control port socket
	bool ProcessControlSocket(bool* InvitationAccepted, bool* InvitationRejected);

	//! Remote partner has asked to close the session
	void PartnerCloseSession(void);
};

#endif
