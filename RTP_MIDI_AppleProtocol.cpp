/*
 *  RTP_MIDI_Input.cpp
 *  Cross-platform RTP-MIDI session initiator/listener endpoint class
 *  Apple session management methods
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

void CRTP_MIDI::SendInvitation (bool DestControl)
{
	// DestControl = true : destination is control port (data port otherwise)
	TSessionPacket Invit;
	sockaddr_in AdrEmit;
	int NameLen;

	NameLen=strlen((char*)&this->SessionName[0]);
	
	Invit.Reserved1=0xFF;
	Invit.Reserved2=0xFF;
	Invit.CommandH='I';
	Invit.CommandL='N';
	Invit.ProtocolVersion=htonl(2);
	Invit.InitiatorToken=htonl(this->InitiatorToken);
	Invit.SSRC=htonl(SSRC);

	if (NameLen>0)  
	{
		strcpy ((char*)&Invit.Name[0], (char*)&this->SessionName[0]);
		Invit.Name[NameLen]=0x00;
		NameLen+=1;
	}
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	AdrEmit.sin_addr.s_addr=htonl(RemoteIPToInvite);
	if (DestControl)
	{
		AdrEmit.sin_port=htons(PartnerControlPort);
		sendto(ControlSocket, (const char*)&Invit, sizeof(TSessionPacketNoName)+NameLen, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
	else
	{
		AdrEmit.sin_port=htons(PartnerDataPort);
		sendto(DataSocket, (const char*)&Invit, sizeof(TSessionPacketNoName)+NameLen, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
} // CRTP_MIDI::SendInvitation
//---------------------------------------------------------------------------

void CRTP_MIDI::SendBYCommand (void)
{
	TSessionPacketNoName PacketBY;
	sockaddr_in AdrEmit;
	
	PacketBY.Reserved1=0xFF;
	PacketBY.Reserved2=0xFF;
	PacketBY.CommandH='B';
	PacketBY.CommandL='Y';
	PacketBY.ProtocolVersion=htonl(2);
	PacketBY.InitiatorToken=htonl(InitiatorToken);
	PacketBY.SSRC=htonl(SSRC);
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	//AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
	AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
	AdrEmit.sin_port=htons(PartnerControlPort);
	sendto(ControlSocket, (const char*)&PacketBY, sizeof(TSessionPacketNoName), 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
} // CRTP_MIDI::SendBYCommand
//---------------------------------------------------------------------------

void CRTP_MIDI::SendInvitationReply (bool FromControlSocket, bool Accept, unsigned int DestinationIP, unsigned short DestinationPort)
{
	TSessionPacket Reply;
	int Size;
	sockaddr_in AdrEmit;
	
	Reply.Reserved1=0xFF;
	Reply.Reserved2=0xFF;
	if (Accept)
	{
		Reply.CommandH='O';
		Reply.CommandL='K';
	}
	else
	{
		Reply.CommandH='N';
		Reply.CommandL='O';
	}
	Reply.ProtocolVersion=htonl(2);
	Reply.InitiatorToken=htonl(this->InitiatorToken);
	Reply.SSRC=htonl(this->SSRC);

	Size=sizeof(TSessionPacketNoName);
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	AdrEmit.sin_addr.s_addr = htonl(DestinationIP);
	AdrEmit.sin_port = htons(DestinationPort);

	if (FromControlSocket)
	{
		sendto(ControlSocket, (const char*)&Reply, Size, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
	else
	{
		sendto(DataSocket, (const char*)&Reply, Size, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
}  // CRTP_MIDI::SendInvitationReply
//---------------------------------------------------------------------------

void CRTP_MIDI::SendSyncPacket (char Count, unsigned int LTS1H, unsigned int LTS1L, unsigned int LTS2H, unsigned int LTS2L, unsigned int LTS3H, unsigned int LTS3L)
{
	TSyncPacket Sync;
	sockaddr_in AdrEmit;
	
	Sync.Reserved1=0xFF;
	Sync.Reserved2=0xFF;
	Sync.CommandH='C';
	Sync.CommandL='K';
	Sync.SSRC=htonl(SSRC);
	Sync.Count=Count;
	Sync.Unused[0]=0;
	Sync.Unused[1]=0;
	Sync.Unused[2]=0;
	Sync.TS1H=htonl(LTS1H);
	Sync.TS1L=htonl(LTS1L);
	Sync.TS2H=htonl(LTS2H);
	Sync.TS2L=htonl(LTS2L);
	Sync.TS3H=htonl(LTS3H);
	Sync.TS3L=htonl(LTS3L);
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
	AdrEmit.sin_port=htons(PartnerDataPort);
	sendto(DataSocket, (const char*)&Sync, sizeof(TSyncPacket), 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
}  // CRTP_MIDI::SendSyncPacket
//---------------------------------------------------------------------------

void CRTP_MIDI::SendFeedbackPacket (unsigned short LastNumber)
{
	TFeedbackPacket Feed;
	sockaddr_in AdrEmit;
	
	Feed.Reserved1=0xFF;
	Feed.Reserved2=0xFF;
	Feed.CommandH='R';
	Feed.CommandL='S';
	Feed.SSRC=htonl(SSRC);
	Feed.SequenceNumber=htons(LastNumber);
	Feed.Unused=0;
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	//AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
	AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
	AdrEmit.sin_port=htons(PartnerControlPort);
	sendto(ControlSocket, (const char*)&Feed, sizeof(TFeedbackPacket), 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
}  // CRTP_MIDI::SendFeedbackPacket
//---------------------------------------------------------------------------


