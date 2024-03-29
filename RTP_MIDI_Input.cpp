/*
 *  RTP_MIDI_Input.cpp
 *  Cross-platform RTP-MIDI session initiator/listener endpoint class
 *  Methods for processing incoming RTP-MIDI messages
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

#include "RTP_MIDI.h"
#include <stdio.h>

unsigned int CRTP_MIDI::GetDeltaTime(unsigned char* BufPtr, int* ByteCtr)
{
	unsigned int value;
	unsigned int ByteCount;
	char Data;

	value=(unsigned int)BufPtr[*ByteCtr];
	*ByteCtr=*ByteCtr+1;
	ByteCount=1;

	if ((value & 0x80)!=0)
	{
		value=value&0x7F;
		do
		{
			Data=BufPtr[*ByteCtr];
			*ByteCtr=*ByteCtr+1;
			ByteCount++;
			value=(value << 7)+(unsigned int)Data;
		} while (((Data & 0x80)!=0)&&(ByteCount<=4));
	}
	return value;
}  // CRTP_MIDI::GetDeltaTime
//--------------------------------------------------------------------------

void CRTP_MIDI::ProcessIncomingRTP (unsigned char* Buffer)
{
	int CtrByteMIDI=0;
	int TailleListeMIDI;
	bool PresenceFirstDelta;
	unsigned char* PtrListeMIDI;
    //unsigned int Timestamp;
    unsigned int DeltaTime;

	TLongMIDIRTPMsg* LInputMessage;
	TShortMIDIRTPMsg* SInputMessage;

	// Store last RTP counter
	SInputMessage=(TShortMIDIRTPMsg*)Buffer;
	LastRTPCounter=htons(SInputMessage->Header.SequenceNumber);

    //Timestamp=htonl(SInputMessage->Header.Timestamp);
    //printf ("Timestamp: %u\n", Timestamp);
    //printf ("Local clock: %u\n", LocalClock);
    //printf ("Difference: %i\n", Timestamp-LocalClock);
    //LastTimestamp=Timestamp;

	// Identify type of RTP MIDI message (short or long) : check B bit in MIDI payload
	if ((SInputMessage->Payload.Control&0x80)!=0)
	{  // B=1 : long block
		LInputMessage=(TLongMIDIRTPMsg*)Buffer;
		LInputMessage->Payload.Control=htons(LInputMessage->Payload.Control);
		TailleListeMIDI=LInputMessage->Payload.Control&0xFFF;
		PresenceFirstDelta=((LInputMessage->Payload.Control&LONG_Z_BIT)!=0);
		PtrListeMIDI=&LInputMessage->Payload.MIDIList[0];
	}
	else
	{	// B=0 : short block
		SInputMessage=(TShortMIDIRTPMsg*)Buffer;
		TailleListeMIDI=SInputMessage->Payload.Control&0xF;
		PresenceFirstDelta=((SInputMessage->Payload.Control&SHORT_Z_BIT)!=0);
		PtrListeMIDI=&SInputMessage->Payload.MIDIList[0];
	}

	if (TailleListeMIDI>0)  // Note : MIDI block can be empty (see protocol specification)
	{
        DeltaTime=0;
		// Analyze first MIDI code
		if (PresenceFirstDelta)
			DeltaTime=GetDeltaTime(&PtrListeMIDI[0], &CtrByteMIDI);
		if (CtrByteMIDI<TailleListeMIDI)  // The last event can be empty (see chapter 3.0 of spec) !
		{
			GenerateMIDIEvent(PtrListeMIDI, &CtrByteMIDI, TailleListeMIDI, DeltaTime+LocalClock);
		}

		// Scan data list
		while (CtrByteMIDI<TailleListeMIDI)
		{
			// Jump over the next timestamp
			DeltaTime=GetDeltaTime(&PtrListeMIDI[0], &CtrByteMIDI);
			if (CtrByteMIDI<TailleListeMIDI)
			{
				GenerateMIDIEvent(PtrListeMIDI, &CtrByteMIDI, TailleListeMIDI, DeltaTime+LocalClock);
			}
		}
	}
}  // CRTP_MIDI::ProcessIncomingRTP
//--------------------------------------------------------------------------

void CRTP_MIDI::GenerateMIDIEvent(unsigned char* Buffer, int* ByteCtr, int TailleBloc, unsigned int LEventTime)
{
	unsigned char DataByte;

	// Decode event type and record it locally
	while (*ByteCtr<TailleBloc)  // Safety measure : do not cross the buffer boundary
	{
		//  Read next byte in RTP block
		DataByte=Buffer[*ByteCtr];
		*ByteCtr+=1;

		// *** SYSEX specific processing ***
		if ((DataByte==0xF0)&&(SYSEX_RTPActif==false))
		{
			// Header F0 received
			SYSEX_RTPActif=true;
			SegmentSYSEXInput=true;
			storeRTP_SYSEXData (0xF0);  // Store SYSEX byte
			goto NextByte;
		}

		if (SYSEX_RTPActif==true)
		{
			if (DataByte==0xF0)
			{
				// F0 of end of segment
				SegmentSYSEXInput=false;
				goto NextByte;									// Stop decoding of the SYSEX message, a new MIDI message is expected
			}

			if (DataByte==0xF7)
			{
				if (SegmentSYSEXInput==true)
				{
					// F7 signalling a end of SYSEX : store the byte
					storeRTP_SYSEXData (0xF7);			// Store SYSEX data
					sendRTP_SYSEXBuffer (LEventTime);				// Send SYSEX buffer to client
					initRTP_SYSEXBuffer ();				// Clean SYSEX buffer
					return;								// Complete MIDI message received
				}
				else
				{
					// F7 signalling a start of segment : do not record
					SegmentSYSEXInput=true;
					goto NextByte;						// Continue SYSEX decoding
				}
			}

			if (DataByte==0xF4)
			{  // SYSEX cancellation code
				initRTP_SYSEXBuffer ();					// Clean SYSEX buffer
				return;									// Stop decoding of the SYSEX message, a new MIDI message is expected
			}

			if (SegmentSYSEXInput)
			{
				// Receving SYSEX data
				if (DataByte<0x80)
				{
					storeRTP_SYSEXData (DataByte);  // Store SYSEX data
					goto NextByte;					// Search next data byte
				}

				if (DataByte>=0xF8)
				{
					// Realtime data in SYSEX : transmit it to client
					FullInMidiMsg[0]=DataByte;
					sendMIDIToClient(1, LEventTime);
					goto NextByte;					// Continue SYSEX decoding
				}

				// Any other data (between 0x80 and 0xF6) : corrupted SYSEX (cancel processing)
				initRTP_SYSEXBuffer();  // Clean SYSEX buffer for next SYSEX message
				// We do not exit here, to process the status byte we just received
			}
		}
		// *** End of SYSEX specific processing ***

		// Not SYSEX or SYSEX interrupted by status byte
		if (DataByte&0x80)
		{	// MSB = 1
			if (DataByte>=0xF8)
			{  // Real time Message
				FullInMidiMsg[0]=DataByte;
				sendMIDIToClient(1, LEventTime);
				return;
			}

			RTPRunningStatus=DataByte;
			FullInMidiMsg[0]=DataByte;
			IncomingThirdByte=false;

			if (DataByte==0xF6)
			{  // Tune Request
				// No need to load FullInMidiMsg[0] with running status : just done before
				sendMIDIToClient(1, LEventTime);
				return;
			}
		}
		else
		{  // MSB = 0
			if (IncomingThirdByte)
			{
				// Full MIDI message on 3 bytes
				FullInMidiMsg[0]=RTPRunningStatus;
				FullInMidiMsg[2]=DataByte;
				IncomingThirdByte=false;
				sendMIDIToClient(3, LEventTime);
				return;
			}

			if (RTPRunningStatus==0) return;  // Ignore data byte

			if (RTPRunningStatus<0xC0)
			{
				// Waiting 3 bytes message
				IncomingThirdByte=true;
				FullInMidiMsg[1]=DataByte;
				goto NextByte;
			}

			if (RTPRunningStatus<0xE0)
			{
				// Full 2 bytes message received
				FullInMidiMsg[0]=RTPRunningStatus;
				FullInMidiMsg[1]=DataByte;
				sendMIDIToClient(2, LEventTime);
				return;
			}

			if (RTPRunningStatus<0xF0)
			{
				// Waiting 3 bytes message
				IncomingThirdByte=true;
				FullInMidiMsg[1]=DataByte;
				goto NextByte;
			}

			if (RTPRunningStatus==0xF2)
			{
				// Waiting 3 bytes message
				RTPRunningStatus=0;
				IncomingThirdByte=true;
				FullInMidiMsg[1]=DataByte;
				goto NextByte;
			}

			if ((RTPRunningStatus==0xF1)||(RTPRunningStatus==0xF3))
			{
				// Message on 2 bytes fully received
				FullInMidiMsg[0]=RTPRunningStatus;
				FullInMidiMsg[1]=DataByte;
				sendMIDIToClient(2, LEventTime);
				RTPRunningStatus=0;
				return;
			}
			// Unsupported bytes
			RTPRunningStatus=0;
			return;
		}  // MSB = 0
	NextByte:
		;
	}  // while
}  // GenerateMIDIEvent
//--------------------------------------------------------------------------

void CRTP_MIDI::initRTP_SYSEXBuffer(void)
{
	InSYSEXBufferPtr=0;
	SegmentSYSEXInput=false;
	SYSEX_RTPActif=false;
	InSYSEXOverflow=false;
}  // CRTP_MIDI::initRTP_SYSEXBuffer
//--------------------------------------------------------------------------

void CRTP_MIDI::storeRTP_SYSEXData (unsigned char SysexData)
{
	if (InSYSEXBuffer==0) return;

	InSYSEXBuffer[InSYSEXBufferPtr]=SysexData;
	if (InSYSEXBufferPtr<InSYSEXBufferSize-1) InSYSEXBufferPtr+=1;
	else InSYSEXOverflow=true;
}  // CRTP_MIDI::storeRTP_SYSEXData
//--------------------------------------------------------------------------

void CRTP_MIDI::sendRTP_SYSEXBuffer (unsigned int LEventTime)
{
	if (RTPCallback==0) return;

	RTPCallback(ClientInstance, InSYSEXBufferPtr, &InSYSEXBuffer[0], LEventTime);
}  // CRTP_MIDI::sendRTP_SYSEXBuffer
//--------------------------------------------------------------------------

void CRTP_MIDI::sendMIDIToClient (unsigned int NumBytes, unsigned int LEventTime)
{
	if (RTPCallback==0) return;

	RTPCallback(ClientInstance, NumBytes, &FullInMidiMsg[0], LEventTime);
}  // CRTP_MIDI::sendRTP_SYSEXBuffer
//--------------------------------------------------------------------------

