/*
 *  RTP_MIDI.cpp
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

/*
 Release notes

8/10/2012
 - bug corrected in ProcessIncomingRTP : missing htons() on LastRTPCounter retrieval

 7/12/2013
 - all long changed to int. Long prevents correct working on 64 bits platform

 03/03/2014
 - modication in RunSession : messages are accepted if they come from associated remote address or if remote address is 0 (used for session listener mode)

 04/04/2014
 - modifications to allow usage with iOS sessions (session names, higher number of sync phases at startup, etc..) after debugging with iOS devices

 03/10/2021
  - two bugs corrected ('=' in place of '==' in tests)
  - removed everything specific to VST plugins
  - removed KissBox hardware identification (useless outside of VSTizers)

10/10/2021
  - added SendMIDIMessage method for mutliple thread support
  - removed parameters for RunSession as it conflicts with the internal buffering model for possible multithreading
  - bug corrected : missing reload of TimeOutRemote in RunSession when we are session initiator

10/04/2023
  - Added automatic port numbering (N+1) when local port is set to 0 (rather two random port numbers) in InitiateSession
  - Remove LocalCtrlPort and LocalDataPort members (not used anywhere)

24/06/2023
  - Moved TS1L, TS1H, TS2L, TS2H, TS3L, TS3H from stack to class members (SessionState==SESSION_CLOCK_SYNC2 uses them after they have been defined from a previous realtime call)

19/11/2023
  - Modification to use updated BEB SDK
  - Code cleaned to remove warnings (unreferenced local variables)

04/01/2024
  - update to MIT license

15/02/2024
  - added capability to reject invitation if session is already opened
  - total cleanup of the RunSession method
  - added some missing __TARGET_LINUX__ directives

16/04/2024
  - added SetCallback method to declare callback and instance outside from constructor

30/06/2024
  - renamed SocketLocked to EndpointLocked
  - bug corrected in SetCallback : endpoint was not locked when callback was set
  - endpoint is now locked when CloseSession is called to allow multiple activation/deactivation
  - init of first timer after session opening is set to 1 ms (otherwise invitation is delayed...)

07/07/2024
  - added Session Name field in Invitation Reply
 */

#include "RTP_MIDI.h"
#include <string.h>
#include <stdlib.h>
#include "SystemSleep.h"
#ifdef SHOW_RTP_INFO
#include <stdio.h>
#endif
#ifdef __TARGET_MAC__
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#endif

CRTP_MIDI::CRTP_MIDI(unsigned int SYXInSize, TRTPMIDIDataCallback CallbackFunc, void* UserInstance)
{
	this->RemoteIPToInvite=0;
	this->PartnerControlPort=0;
	this->PartnerDataPort=0;

	DataSocket=INVALID_SOCKET;
	ControlSocket=INVALID_SOCKET;
	SessionState=SESSION_CLOSED;

    SessionPartnerIP=0;
	strcpy ((char*)&this->SessionName[0], "");

	EndpointLocked=true;
	TimerRunning=false;
	EventTime=0;

	InviteCount=0;
	TimeCounter=0;
	SyncSequenceCounter=0;
	MeasuredLatency = 0xFFFFFFFF;		// Mark as latency not known for now

	this->IsInitiatorNode=true;
	this->TimeOutRemote=4;
	this->ConnectionLost = false;
	this->PeerClosedSession = false;
	this->ConnectionRefused = false;

	InSYSEXBufferSize=SYXInSize;
	InSYSEXBuffer=new unsigned char [InSYSEXBufferSize];

	RTPStreamQueue.ReadPtr = 0;
	RTPStreamQueue.WritePtr = 0;

	initRTP_SYSEXBuffer();

	this->RTPCallback=CallbackFunc;
	this->ClientInstance=UserInstance;
}  // CRTP_MIDI::CRTP_MIDI
//---------------------------------------------------------------------------

CRTP_MIDI::~CRTP_MIDI(void)
{
	CloseSession();
	CloseSockets();

	if (InSYSEXBuffer!=0) delete InSYSEXBuffer;
}  // CRTP_MIDI::~CRTP_MIDI
//---------------------------------------------------------------------------

void CRTP_MIDI::CloseSockets(void)
{
	// Close the UDP sockets
	if (ControlSocket!=INVALID_SOCKET)
		CloseSocket(&ControlSocket);
	if (DataSocket!=INVALID_SOCKET)
		CloseSocket(&DataSocket);
}  // CRTP_MIDI::CloseSockets
//---------------------------------------------------------------------------

void CRTP_MIDI::PrepareTimerEvent (unsigned int TimeToWait)
{
	TimerRunning=false;		// Lock the timer until preparation is done
	EventTime=TimeToWait;
	TimerRunning=true;		// Restart the timer
}  // CRTP_MIDI::PrepareTimerEvent
//---------------------------------------------------------------------------

int CRTP_MIDI::InitiateSession(unsigned int DestIP,
							   unsigned short DestCtrlPort,
							   unsigned short DestDataPort,
							   unsigned short LocalCtrlPort,
							   unsigned short LocalDataPort,
							   bool IsInitiator)
{
    int CreateError=0;
	bool SocketOK;

	this->RemoteIPToInvite=DestIP;
	this->PartnerControlPort=DestCtrlPort;
	this->PartnerDataPort=DestDataPort;

	this->InitiatorToken=rand()*0xFFFFFFFF;
	SSRC=rand()*0xFFFFFFFF;
	RTPSequence=0;
	LastRTPCounter=0;
	LastFeedbackCounter=0;
	SyncSequenceCounter=0;

	// Close the control and data sockets, just in case...
	CloseSockets();

	// Open the two UDP sockets (we let the OS give us the local port number)
	SocketOK=CreateUDPSocket (&ControlSocket, LocalCtrlPort, false);
	if (SocketOK==false) CreateError=-1;
	SocketOK=CreateUDPSocket (&DataSocket, LocalDataPort, false);
	if (SocketOK==false) CreateError=-2;

    if (CreateError!=0)
	{
		CloseSockets();
	}
	else
	{
		// Sockets are opened, we start the session
		SYSEX_RTPActif=false;
		SegmentSYSEXInput=false;
		ConnectionLost = false;
		InviteCount=0;
		TimeOutRemote=16;		// 120 seconds -> Five sync sequences every 1.5 seconds then sync sequence every 10 seconds = 11 + 5
		IncomingThirdByte=false;
		this->IsInitiatorNode=IsInitiator;
		if (IsInitiator==false)
		{  // Do not invite, wait from remote node to start session
			SessionState=SESSION_WAIT_INVITE_CTRL;
		}
		else
		{ // Initiate session by inviting remote node
			SessionState=SESSION_INVITE_CONTROL;
            SessionPartnerIP=RemoteIPToInvite;
		}
		PrepareTimerEvent(1);
		EndpointLocked=false;		// Must be last instruction after session initialization
	}

	return CreateError;
}  // CRTP_MIDI::InitiateSession
//---------------------------------------------------------------------------

void CRTP_MIDI::CloseSession (void)
{
	// Do not send BYE message if we are not completely connected when we are session listener
	if (this->IsInitiatorNode == false)
	{
		if (this->SessionState == SESSION_WAIT_INVITE_CTRL) return;
	}

	if (EndpointLocked) return;

	// Send the message in all other cases, even if we are still in invitation process
	SessionState = SESSION_CLOSED;
	EndpointLocked = true;
	SendBYCommand();
	SystemSleepMillis(50);		// Give time to send the message before closing the sockets
}  // CRTP_MIDI::CloseSession
//---------------------------------------------------------------------------

bool CRTP_MIDI::ProcessControlSocket(bool* InvitationAccepted, bool* InvitationRejected)
{
	int RecvSize;
	unsigned char ReceptionBuffer[1024];
	sockaddr_in SenderData;
	TSessionPacket* SessionPacket;
	unsigned int SenderIP;
	unsigned short SenderPort;

#if defined (__TARGET_MAC__)
	socklen_t fromlen;
#endif
#if defined (__TARGET_LINUX__)
	socklen_t fromlen;
#endif
#if defined (__TARGET_WIN__)
	int fromlen;
#endif

	// Check if something has been received on control socket
	if (DataAvail(ControlSocket, 0))
	{
		fromlen = sizeof(sockaddr_in);
		RecvSize = (int)recvfrom(ControlSocket, (char*)&ReceptionBuffer, sizeof(ReceptionBuffer), 0, (sockaddr*)&SenderData, &fromlen);

		if (RecvSize == 0) return false;

		// Check if this is an Apple session message (ignore every other message received on this socket
		if ((ReceptionBuffer[0] != 0xFF) || (ReceptionBuffer[1] != 0xFF)) return true;

		SenderIP = htonl(SenderData.sin_addr.s_addr);
		SenderPort = htons(SenderData.sin_port);
		SessionPacket = (TSessionPacket*)&ReceptionBuffer[0];

		if ((ReceptionBuffer[2] == 'I') && (ReceptionBuffer[3] == 'N'))
		{  // We are being invited...
			// If we are a session listener, start the invitation acceptance process
			// TODO : if we don't get invitation on data after 5 seconds, return to SESSION_WAIT_INVITE_CTRL state
			if (this->IsInitiatorNode == false)
			{
				if (this->SessionState == SESSION_WAIT_INVITE_CTRL)
				{
					this->InitiatorToken = htonl(SessionPacket->InitiatorToken);
					this->SessionState = SESSION_WAIT_INVITE_DATA;
					PrepareTimerEvent(5000);
					this->SendInvitationReply(true, true, SenderIP, SenderPort);
					this->SessionPartnerIP = SenderIP;
					this->PartnerControlPort = SenderPort;
				}
				else
				{  // We are already in the process of being invited, but this may be a repetition from the same source
					if ((SenderIP == this->SessionPartnerIP) && (SenderPort == this->PartnerControlPort))
					{  // This is a repetition of the invitation we already got : accept it
						PrepareTimerEvent(5000);
						this->SendInvitationReply(true, true, SenderIP, SenderPort);
					}
					else
					{  // Reject invitation from other source
						this->SendInvitationReply(true, false, SenderIP, SenderPort);
					}
				}
			}
			else
			{
				// TODO... (why should be receive an invitation if we are a session initiator) ?
			}
		}
		else if ((ReceptionBuffer[2] == 'O') && (ReceptionBuffer[3] == 'K'))
		{  // Remote device accepted our invitation
			*InvitationAccepted = true;
		}
		else if ((ReceptionBuffer[2] == 'N') && (ReceptionBuffer[3] == 'O'))
		{  // Remote device rejected our invitation
			*InvitationRejected = true;
		}
		else if ((ReceptionBuffer[2] == 'B') && (ReceptionBuffer[3] == 'Y'))
		{  // Remote device closes the session
			if (SenderIP == this->SessionPartnerIP)  // Only accept BY message from the connected partner
			{
				this->PartnerCloseSession();
			}
		}
		return true;
	}  // Data available on control socket
	else return false;
}  // CRTP_MIDI::ProcessControlSocket
//---------------------------------------------------------------------------

void CRTP_MIDI::PartnerCloseSession(void)
{
	this->TimerRunning = false;     // Stop any timed event
	if (this->IsInitiatorNode == false)
	{
		SessionState = SESSION_WAIT_INVITE_CTRL;
	}
	else
	{
		SessionState = SESSION_CLOSED;
	}
	this->PeerClosedSession = true;
	this->SessionPartnerIP = 0;
}  // CRTP_MIDI::PartnerCloseSession
//---------------------------------------------------------------------------

void CRTP_MIDI::RunSession(void)
{
	bool TimerEvent = false;
	unsigned int SenderIP;
	unsigned short SenderPort;
	int RecvSize;
	unsigned char ReceptionBuffer[1024];
	sockaddr_in SenderData;
	TSyncPacket* SyncPacket;
	TLongMIDIRTPMsg LRTPMessage;
	sockaddr_in AdrEmit;
	int RTPOutSize;
	bool InvitationAcceptedOnCtrl;
	bool InvitationRejectedOnCtrl;
	bool InvitationAcceptedOnData;
	bool InvitationRejectedOnData;
	bool PacketReceivedOnControl;
	bool PacketReceivedOnData;

#if defined (__TARGET_MAC__)
	socklen_t fromlen;
#endif
#if defined (__TARGET_LINUX__)
	socklen_t fromlen;
#endif
#if defined (__TARGET_WIN__)
	int fromlen;
#endif

	// Computing time using the thread is not perfect, we should use OS time related data
	// timeGetTime can be used on Windows, but no direct equivalent in Mac or Linux
	this->TimeCounter += 10;
	this->LocalClock += 10;

	// Do not process if communication layers are not ready
	if (this->EndpointLocked) return;

	// Check if timer elapsed
	if (this->TimerRunning)
	{
		if (this->EventTime > 0)
			this->EventTime--;
		if (EventTime == 0)
		{
			this->TimerRunning = false;
			TimerEvent = true;
		}
	}

	// If we are being invited but invitation process does not complete in time, return to listener state
	if ((TimerEvent) && (this->SessionState == SESSION_WAIT_INVITE_DATA))
	{
		this->SessionState = SESSION_WAIT_INVITE_CTRL;
	}
	if ((TimerEvent) && (this->SessionState == SESSION_WAIT_CLOCK_SYNC))
	{
		this->SessionState = SESSION_WAIT_INVITE_CTRL;
	}

	InvitationAcceptedOnCtrl = false;
	InvitationRejectedOnCtrl = false;
	InvitationAcceptedOnData = false;
	InvitationRejectedOnData = false;
	// We have to loop until control and data sockets are flushed, as this method is called every 1ms
	// Otherwise we may introduce processing delays if there are bursts of packets to these ports
	do
	{
		PacketReceivedOnControl = this->ProcessControlSocket(&InvitationAcceptedOnCtrl, &InvitationRejectedOnCtrl);

		// Process incoming packets on data socket
		PacketReceivedOnData = false;
		if (DataAvail(DataSocket, 0))
		{
			PacketReceivedOnData = true;
			fromlen = sizeof(sockaddr_in);
			RecvSize = (int)recvfrom(DataSocket, (char*)&ReceptionBuffer, sizeof(ReceptionBuffer), 0, (sockaddr*)&SenderData, &fromlen);

			if (RecvSize > 0)
			{
				SenderIP = htonl(SenderData.sin_addr.s_addr);
				SenderPort = htons(SenderData.sin_port);

				if (SenderIP == this->SessionPartnerIP)
				{
					// Process incoming RTP-MIDI packet
					if ((ReceptionBuffer[0] == 0x80) && (ReceptionBuffer[1] == 0x61))  // Check Apple RTP-MIDI packet signature
					{
						if (this->SessionState == SESSION_OPENED)
						{
							ProcessIncomingRTP(&ReceptionBuffer[0]);
						}
					}

					else if ((ReceptionBuffer[0] == 0xFF) && (ReceptionBuffer[1] == 0xFF))
					{
						if ((ReceptionBuffer[2] == 'C') && (ReceptionBuffer[3] == 'K'))
						{  // Process clock message first as they come more often than other session messages
							SyncPacket = (TSyncPacket*)&ReceptionBuffer[0];

							if (SyncPacket->Count == 0)
							{
								this->TS1H = htonl(SyncPacket->TS1H);
								this->TS1L = htonl(SyncPacket->TS1L);
								SendSyncPacket(1, this->TS1H, this->TS1L, 0, TimeCounter, 0, 0);
							}
							else if (SyncPacket->Count == 1)
							{
								this->TS1H = htonl(SyncPacket->TS1H);
								this->TS1L = htonl(SyncPacket->TS1L);
								this->TS2H = htonl(SyncPacket->TS2H);
								this->TS2L = htonl(SyncPacket->TS2L);
								this->MeasuredLatency = TimeCounter - TS1L;

								this->TimeOutRemote = 4;
								this->SendSyncPacket(2, TS1H, TS1L, TS2H, TS2L, 0, TimeCounter);
								if ((this->IsInitiatorNode) && (SessionState == SESSION_CLOCK_SYNC1))
								{
									this->TimeOutRemote = 4;
									this->SessionState = SESSION_OPENED;
								}
							}
							else if (SyncPacket->Count == 2)
							{
								this->TS1H = htonl(SyncPacket->TS1H);
								this->TS1L = htonl(SyncPacket->TS1L);
								this->TS2H = htonl(SyncPacket->TS2H);
								this->TS2L = htonl(SyncPacket->TS2L);
								this->TS3H = htonl(SyncPacket->TS3H);
								this->TS3L = htonl(SyncPacket->TS3L);
								this->MeasuredLatency = TimeCounter - TS2L;
								this->TimeOutRemote = 4;
								this->SessionState = SESSION_OPENED;
							}
						}  // CK message
						else if ((ReceptionBuffer[2] == 'I') && (ReceptionBuffer[3] == 'N'))
						{  // Accept invitation
							this->SessionState = SESSION_WAIT_CLOCK_SYNC;
							PrepareTimerEvent(2000);
							this->SendInvitationReply(false, true, SenderIP, SenderPort);
							this->PartnerDataPort = SenderPort;
						}
						else if ((ReceptionBuffer[2] == 'O') && (ReceptionBuffer[3] == 'K'))
						{  // Remote device accepted our invitation
							InvitationAcceptedOnData = true;
						}
						else if ((ReceptionBuffer[2] == 'N') && (ReceptionBuffer[3] == 'O'))
						{  // Remote device rejected our invitation
							InvitationRejectedOnData = true;
						}
						else if ((ReceptionBuffer[2] == 'B') && (ReceptionBuffer[3] == 'Y'))
						{
							this->PartnerCloseSession();
						}
					}
				}  // Packet sent from remote partner
			}  // Received packet size > 0
		}  // UDP packet available on data socket
	} while (PacketReceivedOnControl||PacketReceivedOnData);

	// Terminate the session if remote device has rejected our invitation
	if (InvitationRejectedOnCtrl || InvitationRejectedOnData)
	{
		this->PartnerCloseSession();
		this->ConnectionRefused = true;
		// Just in case we got also a session accepted...
		InvitationAcceptedOnData = false;
		InvitationAcceptedOnCtrl = false;
	}

	// Run session initiator
	if (this->IsInitiatorNode)
	{
		if (SessionState == SESSION_INVITE_CONTROL)
		{
			SyncSequenceCounter = 0;
			if (InvitationAcceptedOnCtrl)
			{
				SessionState = SESSION_INVITE_DATA;
				this->SendInvitation(false);
				PrepareTimerEvent(100);
				return;
			}
			else if (TimerRunning == false)
			{
				if (TimerEvent)
				{  // Previous attempt has timed out
					// Keep inviting until we get an answer
					{
						this->SendInvitation(true);
						PrepareTimerEvent(1000);  // Wait one second before sending a new invitation
						InviteCount++;
					}
				}
			}
		}  // Inviting on control port

		else if (SessionState == SESSION_INVITE_DATA)
		{
			if (InvitationAcceptedOnData)
			{
				SessionState = SESSION_CLOCK_SYNC0;
			}
			else if (TimerRunning == false)
			{
				if (TimerEvent)
				{  // Previous attempt has timed out
					if (InviteCount > 12)
					{  // No answer received from remote station after 12 attempts : stop invitation and go back to SESSION_INVITE_CONTROL
						RestartSession();
						return;
					}
					else
					{
						this->SendInvitation(false);
						PrepareTimerEvent(1000);  // Wait one second before sending a new invitation
						InviteCount++;
						return;
					}
				}
			}
		}

		else if (SessionState == SESSION_CLOCK_SYNC0)
		{
			SendSyncPacket(0, 0, TimeCounter, 0, 0, 0, 0);
			SessionState = SESSION_CLOCK_SYNC1;
		}
	}

	// Process RTP communication and feedback when session is opened
	if (this->SessionState == SESSION_OPENED)
	{
		RTPOutSize = PrepareMessage(&LRTPMessage, TimeCounter);
		if (RTPOutSize > 0)
		{
			this->RTPSequence++;  // Increment for next message
			// Send message on network
			memset(&AdrEmit, 0, sizeof(sockaddr_in));
			AdrEmit.sin_family = AF_INET;
			AdrEmit.sin_addr.s_addr = htonl(this->SessionPartnerIP);
			AdrEmit.sin_port = htons(this->PartnerDataPort);
			sendto(DataSocket, (const char*)&LRTPMessage, RTPOutSize, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
		}

		// When session is opened, the timer keeps running
		if (TimerEvent)
		{
			// Send a RS packet if we have received something meanwhile (do not send the RS if nothing has been received, it crashes the Apple driver)
			if (this->LastRTPCounter != this->LastFeedbackCounter)
			{
				this->SendFeedbackPacket(LastRTPCounter);
				this->LastFeedbackCounter = this->LastRTPCounter;
			}

			if (this->IsInitiatorNode == true)
			{  // Restart a synchronization sequence if we are session initiator
				this->SendSyncPacket(0, 0, TimeCounter, 0, 0, 0, 0);
			}

			// We send first a sync sequence 5 times every 1.5 seconds, then one sync sequence every 10 seconds
			if (this->SyncSequenceCounter <= 5)
			{
				this->PrepareTimerEvent(1500);
				this->SyncSequenceCounter += 1;
			}
			else
			{
				this->PrepareTimerEvent(10000);
			}
			if (this->TimeOutRemote > 0)
				this->TimeOutRemote -= 1;
		}

		// If communication with remote device times out, we consider it has disconnected without sending BYE
		if (this->TimeOutRemote == 0)
		{
			this->ConnectionLost = true;
			if (this->IsInitiatorNode)
			{  // Restart invitation sequence
				this->TimeOutRemote = 4;
				this->RestartSession();
			}
			else
			{  // If we are not session initiator, just wait to be invited again
				this->SessionState = SESSION_WAIT_INVITE_CTRL;
			}
		}
	}  // Session is opened
}  // CRTP_MIDI::RunSession
//---------------------------------------------------------------------------

int CRTP_MIDI::GeneratePayload (unsigned char* MIDIList)
{
	unsigned int MIDIBlockEnd;			// Index of last MIDI byte in FIFO to transmit
	bool FullPayload;					// RTP payload block full of data
	int CtrBytePayload;					// Counter of RTP payload bytes
	unsigned int TempPtr;

	CtrBytePayload=0;
	FullPayload=false;

	// Check if we have data in the RTP stream queue
    MIDIBlockEnd=RTPStreamQueue.WritePtr;			// Snapshot of current position of last MIDI message
    if (MIDIBlockEnd!=RTPStreamQueue.ReadPtr)
    {
		while ((RTPStreamQueue.ReadPtr!=MIDIBlockEnd)&&(!FullPayload))
        {
			// TODO : make sure that there is enough room in the RTP-MIDI message buffer
            TempPtr=RTPStreamQueue.ReadPtr;
            MIDIList[CtrBytePayload]=RTPStreamQueue.FIFO[TempPtr];
            CtrBytePayload+=1;

            TempPtr+=1;
            if (TempPtr>=MIDI_CHAR_FIFO_SIZE)
				TempPtr=0;
            RTPStreamQueue.ReadPtr=TempPtr;		// Update pointer only after checking we did not loopback
        }
    }

	return CtrBytePayload;
}  // CRTP_MIDI::GeneratePayload
//--------------------------------------------------------------------------

int CRTP_MIDI::PrepareMessage (TLongMIDIRTPMsg* Buffer, unsigned int TimeStamp)
{
	unsigned int TailleMIDI;

	TailleMIDI=GeneratePayload(&Buffer->Payload.MIDIList[0]);
	if (TailleMIDI==0) return 0;  // No MIDI data to transmit

	// Write directly value rather than bit coding
	// Version=2, Padding=0, Extension=0, CSRCCount=0, Marker=1, PayloadType=0x11
	Buffer->Header.Code1=0x80;
	Buffer->Header.Code2=0x61;

	// Long MIDI list : B=1
	// Phantom = 0 (status byte always included)
	Buffer->Payload.Control=htons((unsigned short)TailleMIDI|LONG_B_BIT);

	Buffer->Header.SequenceNumber=htons(RTPSequence);
	Buffer->Header.Timestamp=htonl(TimeStamp);
	Buffer->Header.SSRC=htonl(SSRC);
	return TailleMIDI+sizeof(TRTP_Header)+2;  // 2 = size of control word
}  // CRTP_MIDI::PrepareMessage
//--------------------------------------------------------------------------

int CRTP_MIDI::getSessionStatus (void)
{
	if (SessionState==SESSION_CLOSED) return 0;
	if (SessionState==SESSION_OPENED) return 3;
	if ((SessionState==SESSION_INVITE_DATA)||(SessionState==SESSION_INVITE_CONTROL)) return 1;
	return 2;
}  // CRTP_MIDI::getSessionStatus
//--------------------------------------------------------------------------

void CRTP_MIDI::setSessionName (char* Name)
{
	if (strlen(Name)>MAX_SESSION_NAME_LEN-1) return;
	strcpy ((char*)&this->SessionName[0], Name);
}  // CRTPMIDI::setSessionName
//--------------------------------------------------------------------------

bool CRTP_MIDI::SendRTPMIDIBlock (unsigned int BlockSize, unsigned char* MIDIData)
{
	// TODO : add a semaphore in case this function is called from another thread
	unsigned int TmpWrite;
	unsigned int ByteCounter;

	if (BlockSize == 0) return true;
	if (SessionState!=SESSION_OPENED) return false;		// Avoid filling the FIFO when nothing can be sent

	// Try to copy the whole block in FIFO
	TmpWrite = RTPStreamQueue.WritePtr;

	for (ByteCounter=0; ByteCounter<BlockSize; ByteCounter++)
	{
		RTPStreamQueue.FIFO[TmpWrite] = MIDIData[ByteCounter];
		TmpWrite++;
		if (TmpWrite>=MIDI_CHAR_FIFO_SIZE)
			TmpWrite = 0;

		// Check if FIFO is not full
		if (TmpWrite == RTPStreamQueue.ReadPtr) return false;
	}

	// Update write pointer only when the whole block has been copied
	RTPStreamQueue.WritePtr = TmpWrite;

	return true;
}  // CRTP_MIDI::SendRTPMIDIBlock
//--------------------------------------------------------------------------

unsigned int CRTP_MIDI::GetLatency (void)
{
	if (SessionState != SESSION_OPENED) return 0xFFFFFFFF;

	return MeasuredLatency;
}  // CRTP_MIDI::GetLatency
//--------------------------------------------------------------------------

void CRTP_MIDI::RestartSession (void)
{
	if (this->IsInitiatorNode == false) return;

	SYSEX_RTPActif=false;
	SegmentSYSEXInput=false;
	InviteCount=0;
	TimeOutRemote=16;		// 120 seconds -> Five sync sequences every 1.5 seconds then sync sequence every 10 seconds = 11 + 5
	IncomingThirdByte=false;
	SessionState=SESSION_INVITE_CONTROL;
    PrepareTimerEvent(1000);
}  // CRTP_MIDI::RestartSession
//--------------------------------------------------------------------------

bool CRTP_MIDI::ReadAndResetConnectionLost (void)
{
	if (this->ConnectionLost==false) return false;

	this->ConnectionLost=false;
	return true;
}  // CRTP_MIDI::ReadAndResetConnectionLost
//--------------------------------------------------------------------------

bool CRTP_MIDI::RemotePeerHasClosedSession (void)
{
	bool ReadValue;

	ReadValue = this->PeerClosedSession;
	this->PeerClosedSession = false;

	return ReadValue;
}  // CRTP_MIDI::RemotePeerClosedSession
//--------------------------------------------------------------------------

bool CRTP_MIDI::RemotePeerHasRefusedSession(void)
{
	bool ReadValue;

	ReadValue = this->ConnectionRefused;
	this->ConnectionRefused = false;

	return ReadValue;
}  // CRTP_MIDI::RemotePeerHasRefusedSession
//--------------------------------------------------------------------------

void CRTP_MIDI::SetCallback(TRTPMIDIDataCallback CallbackFunc, void* UserInstance)
{
	bool SocketState = this->EndpointLocked;

	this->EndpointLocked = true;		// Block processing to avoid callbacks while we configure them

	this->ClientInstance = UserInstance;
	this->RTPCallback = CallbackFunc;

	// Restore lock state
	this->EndpointLocked = SocketState;
}  // CRTP_MIDI::SetCallback
//--------------------------------------------------------------------------
