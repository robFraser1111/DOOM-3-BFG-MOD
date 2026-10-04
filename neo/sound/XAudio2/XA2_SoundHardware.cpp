/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../snd_local.h"
#include "../../../doomclassic/doom/i_sound.h"

// idStr.h defines StrCmp* as macros. shlwapi.h (pulled in by the device headers) defines the same names.
#undef StrCmpN
#undef StrCmpNI
#undef StrCmpI
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

/*
========================
xa2AudioDevice_t

Active render endpoints from WASAPI. XAudio2 2.9 no longer exposes
IXAudio2::GetDeviceCount / GetDeviceDetails (those were XAudio2 2.7).
========================
*/
struct xa2AudioDevice_t {
	WCHAR	id[512];
	char	name[256];
	UINT32	channels;
	UINT32	sampleRate;
	DWORD	channelMask;
};

static void EnumerateRenderDevices( idList<xa2AudioDevice_t> &devices ) {
	devices.Clear();

	IMMDeviceEnumerator *enumerator = NULL;
	HRESULT hr = CoCreateInstance( __uuidof( MMDeviceEnumerator ), NULL, CLSCTX_ALL, __uuidof( IMMDeviceEnumerator ), (void **)&enumerator );
	if ( FAILED( hr ) || enumerator == NULL ) {
		return;
	}

	IMMDeviceCollection *collection = NULL;
	hr = enumerator->EnumAudioEndpoints( eRender, DEVICE_STATE_ACTIVE, &collection );
	if ( FAILED( hr ) || collection == NULL ) {
		enumerator->Release();
		return;
	}

	UINT count = 0;
	collection->GetCount( &count );
	for ( UINT index = 0; index < count; index++ ) {
		IMMDevice *device = NULL;
		if ( FAILED( collection->Item( index, &device ) ) || device == NULL ) {
			continue;
		}

		xa2AudioDevice_t info;
		memset( &info, 0, sizeof( info ) );
		info.channels = 2;
		info.sampleRate = 44100;
		info.channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

		LPWSTR endpointId = NULL;
		if ( SUCCEEDED( device->GetId( &endpointId ) ) && endpointId != NULL ) {
			wcsncpy( info.id, endpointId, ( sizeof( info.id ) / sizeof( info.id[0] ) ) - 1 );
			CoTaskMemFree( endpointId );
		}

		IPropertyStore *store = NULL;
		if ( SUCCEEDED( device->OpenPropertyStore( STGM_READ, &store ) ) && store != NULL ) {
			PROPVARIANT friendly;
			PropVariantInit( &friendly );
			if ( SUCCEEDED( store->GetValue( PKEY_Device_FriendlyName, &friendly ) ) && friendly.vt == VT_LPWSTR && friendly.pwszVal != NULL ) {
				wcstombs( info.name, friendly.pwszVal, sizeof( info.name ) - 1 );
			}
			PropVariantClear( &friendly );
			store->Release();
		}
		if ( info.name[0] == '\0' ) {
			idStr::Copynz( info.name, "Audio Device", sizeof( info.name ) );
		}

		IAudioClient *client = NULL;
		if ( SUCCEEDED( device->Activate( __uuidof( IAudioClient ), CLSCTX_ALL, NULL, (void **)&client ) ) && client != NULL ) {
			WAVEFORMATEX *mix = NULL;
			if ( SUCCEEDED( client->GetMixFormat( &mix ) ) && mix != NULL ) {
				info.channels = mix->nChannels;
				info.sampleRate = mix->nSamplesPerSec;
				if ( mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE ) {
					info.channelMask = reinterpret_cast<WAVEFORMATEXTENSIBLE *>( mix )->dwChannelMask;
				}
				CoTaskMemFree( mix );
			}
			client->Release();
		}

		devices.Append( info );
		device->Release();
	}

	collection->Release();
	enumerator->Release();
}

idCVar s_showLevelMeter( "s_showLevelMeter", "0", CVAR_BOOL|CVAR_ARCHIVE, "Show VU meter" );
idCVar s_meterTopTime( "s_meterTopTime", "1000", CVAR_INTEGER|CVAR_ARCHIVE, "How long (in milliseconds) peaks are displayed on the VU meter" );
idCVar s_meterPosition( "s_meterPosition", "100 100 20 200", CVAR_ARCHIVE, "VU meter location (x y w h)" );
idCVar s_device( "s_device", "-1", CVAR_INTEGER|CVAR_ARCHIVE, "Which audio device to use (listDevices to list, -1 for default)" );
idCVar s_showPerfData( "s_showPerfData", "0", CVAR_BOOL, "Show XAudio2 Performance data" );
extern idCVar s_volume_dB;

/*
========================
idSoundHardware_XAudio2::idSoundHardware_XAudio2
========================
*/
idSoundHardware_XAudio2::idSoundHardware_XAudio2() {
	pXAudio2 = NULL;
	pMasterVoice = NULL;
	pSubmixVoice = NULL;

	vuMeterRMS = NULL;
	vuMeterPeak = NULL;

	outputChannels = 0;
	channelMask = 0;

	voices.SetNum( 0 );
	zombieVoices.SetNum( 0 );
	freeVoices.SetNum( 0 );

	lastResetTime = 0;
}

void listDevices_f( const idCmdArgs & args ) {
	idList<xa2AudioDevice_t> devices;
	EnumerateRenderDevices( devices );
	if ( devices.Num() == 0 ) {
		idLib::Warning( "No audio devices found" );
		return;
	}

	for ( int i = 0; i < devices.Num(); i++ ) {
		idLib::Printf( "%3d: %s\n", i, devices[i].name );
		idLib::Printf( "     %d channels, %d Hz, mask 0x%x\n", devices[i].channels, devices[i].sampleRate, devices[i].channelMask );
	}
	idLib::Printf( "Use s_device N to select one, or -1 for the Windows default.\n" );
}

/*
========================
idSoundHardware_XAudio2::Init
========================
*/
void idSoundHardware_XAudio2::Init() {

	cmdSystem->AddCommand( "listDevices", listDevices_f, 0, "Lists the connected sound devices", NULL );

	// XAudio2 2.9 has no XAUDIO2_DEBUG_ENGINE flag. Debug tracing is SetDebugConfiguration.
	if ( FAILED( XAudio2Create( &pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR ) ) ) {
		idLib::FatalError( "Failed to create XAudio2 engine. XAudio2 2.9 is part of Windows 8 and later." );
		return;
	}
#ifdef _DEBUG
	XAUDIO2_DEBUG_CONFIGURATION debugConfiguration = { 0 };
	debugConfiguration.TraceMask = XAUDIO2_LOG_WARNINGS;
	debugConfiguration.BreakMask = XAUDIO2_LOG_ERRORS;
	pXAudio2->SetDebugConfiguration( &debugConfiguration );
#endif

	// Register the sound engine callback
	pXAudio2->RegisterForCallbacks( &soundEngineCallback );
	soundEngineCallback.hardware = this;

	idList<xa2AudioDevice_t> devices;
	EnumerateRenderDevices( devices );

	idCmdArgs args;
	listDevices_f( args );

	int preferredDevice = s_device.GetInteger();
	const WCHAR *deviceId = NULL;
	if ( preferredDevice >= 0 && preferredDevice < devices.Num() && devices[preferredDevice].id[0] != L'\0' ) {
		deviceId = devices[preferredDevice].id;
		idLib::Printf( "Using device %d (%s)\n", preferredDevice, devices[preferredDevice].name );
	} else {
		preferredDevice = -1;
		idLib::Printf( "Using default audio device\n" );
	}

	// Keep the original 44.1 kHz mastering rate. XAudio2 2.9 takes a WASAPI endpoint id, not a device index.
	DWORD outputSampleRate = 44100;
	if ( FAILED( pXAudio2->CreateMasteringVoice( &pMasterVoice, XAUDIO2_DEFAULT_CHANNELS, outputSampleRate, 0, deviceId, NULL ) ) ) {
		if ( deviceId != NULL && SUCCEEDED( pXAudio2->CreateMasteringVoice( &pMasterVoice, XAUDIO2_DEFAULT_CHANNELS, outputSampleRate, 0, NULL, NULL ) ) ) {
			idLib::Warning( "Failed to open the selected audio device; using the default" );
			preferredDevice = -1;
		} else {
			idLib::Warning( "Failed to create master voice" );
			pXAudio2->Release();
			pXAudio2 = NULL;
			return;
		}
	}
	pMasterVoice->SetVolume( DBtoLinear( s_volume_dB.GetFloat() ) );

	DWORD masteredMask = 0;
	pMasterVoice->GetChannelMask( &masteredMask );
	XAUDIO2_VOICE_DETAILS voiceDetails;
	pMasterVoice->GetVoiceDetails( &voiceDetails );
	outputChannels = (int)voiceDetails.InputChannels;
	channelMask = masteredMask;
	if ( outputChannels <= 0 ) {
		outputChannels = ( preferredDevice >= 0 ) ? (int)devices[preferredDevice].channels : 2;
	}
	if ( channelMask == 0 ) {
		channelMask = ( preferredDevice >= 0 ) ? devices[preferredDevice].channelMask : ( SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT );
	}

	idSoundVoice::InitSurround( outputChannels, channelMask );

	// ---------------------
	// Initialize the Doom classic sound system.
	// ---------------------
	I_InitSoundHardware( outputChannels, channelMask );

	// ---------------------
	// Create VU Meter Effect
	// ---------------------
	IUnknown * vuMeter = NULL;
	XAudio2CreateVolumeMeter( &vuMeter, 0 );

	XAUDIO2_EFFECT_DESCRIPTOR descriptor;
	descriptor.InitialState = true;
	descriptor.OutputChannels = outputChannels;
	descriptor.pEffect = vuMeter;

	XAUDIO2_EFFECT_CHAIN chain;
	chain.EffectCount = 1;
	chain.pEffectDescriptors = &descriptor;

	pMasterVoice->SetEffectChain( &chain );

	vuMeter->Release();

	// ---------------------
	// Create VU Meter Graph
	// ---------------------

	vuMeterRMS = console->CreateGraph( outputChannels );
	vuMeterPeak = console->CreateGraph( outputChannels );
	vuMeterRMS->Enable( false );
	vuMeterPeak->Enable( false );

	memset( vuMeterPeakTimes, 0, sizeof( vuMeterPeakTimes ) );

	vuMeterPeak->SetFillMode( idDebugGraph::GRAPH_LINE );
	vuMeterPeak->SetBackgroundColor( idVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );

	vuMeterRMS->AddGridLine( 0.500f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
	vuMeterRMS->AddGridLine( 0.250f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
	vuMeterRMS->AddGridLine( 0.125f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );

	const char * channelNames[] = { "L", "R", "C", "S", "Lb", "Rb", "Lf", "Rf", "Cb", "Ls", "Rs" };
	for ( int i = 0, ci = 0; ci < sizeof( channelNames ) / sizeof( channelNames[0] ); ci++ ) {
		if ( ( channelMask & BIT( ci ) ) == 0 ) {
			continue;
		}
		vuMeterRMS->SetLabel( i, channelNames[ ci ] );
		i++;
	}

	// ---------------------
	// Create submix buffer
	// ---------------------
	if ( FAILED( pXAudio2->CreateSubmixVoice( &pSubmixVoice, 1, outputSampleRate, 0, 0, NULL, NULL ) ) ) {
		idLib::FatalError( "Failed to create submix voice" );
	}

	// XAudio doesn't really impose a maximum number of voices
	voices.SetNum( voices.Max() );
	freeVoices.SetNum( voices.Max() );
	zombieVoices.SetNum( 0 );
	for ( int i = 0; i < voices.Num(); i++ ) {
		freeVoices[i] = &voices[i];
	}
}

/*
========================
idSoundHardware_XAudio2::Shutdown
========================
*/
void idSoundHardware_XAudio2::Shutdown() {
	for ( int i = 0; i < voices.Num(); i++ ) {
		voices[ i ].DestroyInternal();
	}
	voices.Clear();
	freeVoices.Clear();
	zombieVoices.Clear();

	// ---------------------
	// Shutdown the Doom classic sound system.
	// ---------------------
	I_ShutdownSoundHardware();

	if ( pXAudio2 != NULL ) {
		// Unregister the sound engine callback
		pXAudio2->UnregisterForCallbacks( &soundEngineCallback );
	}

	if ( pSubmixVoice != NULL ) {
		pSubmixVoice->DestroyVoice();
		pSubmixVoice = NULL;
	}
	if ( pMasterVoice != NULL ) {
		// release the vu meter effect
		pMasterVoice->SetEffectChain( NULL );
		pMasterVoice->DestroyVoice();
		pMasterVoice = NULL;
	}
	if ( pXAudio2 != NULL ) {
		XAUDIO2_PERFORMANCE_DATA perfData;
		pXAudio2->GetPerformanceData( &perfData );
		idLib::Printf( "Final pXAudio2 performanceData: Voices: %d/%d CPU: %.2f%% Mem: %dkb\n", perfData.ActiveSourceVoiceCount, perfData.TotalSourceVoiceCount, perfData.AudioCyclesSinceLastQuery / (float)perfData.TotalCyclesSinceLastQuery, perfData.MemoryUsageInBytes / 1024 );
		pXAudio2->Release();
		pXAudio2 = NULL;
	}
	if ( vuMeterRMS != NULL ) {
		console->DestroyGraph( vuMeterRMS );
		vuMeterRMS = NULL;
	}
	if ( vuMeterPeak != NULL ) {
		console->DestroyGraph( vuMeterPeak );
		vuMeterPeak = NULL;
	}
}

/*
========================
idSoundHardware_XAudio2::AllocateVoice
========================
*/
idSoundVoice * idSoundHardware_XAudio2::AllocateVoice( const idSoundSample * leadinSample, const idSoundSample * loopingSample ) {
	if ( leadinSample == NULL ) {
		return NULL;
	}
	if ( loopingSample != NULL ) {
		if ( ( leadinSample->format.basic.formatTag != loopingSample->format.basic.formatTag ) || ( leadinSample->format.basic.numChannels != loopingSample->format.basic.numChannels ) ) {
			idLib::Warning( "Leadin/looping format mismatch: %s & %s", leadinSample->GetName(), loopingSample->GetName() );
			loopingSample = NULL;
		}
	}

	// Try to find a free voice that matches the format
	// But fallback to the last free voice if none match the format
	idSoundVoice * voice = NULL;
	for ( int i = 0; i < freeVoices.Num(); i++ ) {
		if ( freeVoices[i]->IsPlaying() ) {
			continue;
		}
		voice = (idSoundVoice *)freeVoices[i];
		if ( voice->CompatibleFormat( (idSoundSample_XAudio2*)leadinSample ) ) {
			break;
		}
	}
	if ( voice != NULL ) {
		voice->Create( leadinSample, loopingSample );
		freeVoices.Remove( voice );
		return voice;
	}
	
	return NULL;
}

/*
========================
idSoundHardware_XAudio2::FreeVoice
========================
*/
void idSoundHardware_XAudio2::FreeVoice( idSoundVoice * voice ) {
	voice->Stop();

	// Stop() is asyncronous, so we won't flush bufferes until the
	// voice on the zombie channel actually returns !IsPlaying() 
	zombieVoices.Append( voice );
}

/*
========================
idSoundHardware_XAudio2::Update
========================
*/
void idSoundHardware_XAudio2::Update() {
	if ( pXAudio2 == NULL ) {
		int nowTime = Sys_Milliseconds();
		if ( lastResetTime + 1000 < nowTime ) {
			lastResetTime = nowTime;
			Init();
		}
		return;
	}
	if ( soundSystem->IsMuted() ) {
		pMasterVoice->SetVolume( 0.0f, OPERATION_SET );
	} else {
		pMasterVoice->SetVolume( DBtoLinear( s_volume_dB.GetFloat() ), OPERATION_SET );
	}

	pXAudio2->CommitChanges( XAUDIO2_COMMIT_ALL );

	// IXAudio2SourceVoice::Stop() has been called for every sound on the
	// zombie list, but it is documented as asyncronous, so we have to wait
	// until it actually reports that it is no longer playing.
	for ( int i = 0; i < zombieVoices.Num(); i++ ) {
		zombieVoices[i]->FlushSourceBuffers();
		if ( !zombieVoices[i]->IsPlaying() ) {
			freeVoices.Append( zombieVoices[i] );
			zombieVoices.RemoveIndexFast( i );
			i--;
		} else {
			static int playingZombies;
			playingZombies++;
		}
	}

	if ( s_showPerfData.GetBool() ) {
		XAUDIO2_PERFORMANCE_DATA perfData;
		pXAudio2->GetPerformanceData( &perfData );
		idLib::Printf( "Voices: %d/%d CPU: %.2f%% Mem: %dkb\n", perfData.ActiveSourceVoiceCount, perfData.TotalSourceVoiceCount, perfData.AudioCyclesSinceLastQuery / (float)perfData.TotalCyclesSinceLastQuery, perfData.MemoryUsageInBytes / 1024 );
	}

	if ( vuMeterRMS == NULL ) {
		// Init probably hasn't been called yet
		return;
	}

	vuMeterRMS->Enable( s_showLevelMeter.GetBool() );
	vuMeterPeak->Enable( s_showLevelMeter.GetBool() );

	if ( !s_showLevelMeter.GetBool() ) {
		pMasterVoice->DisableEffect( 0 );
		return;
	} else {
		pMasterVoice->EnableEffect( 0 );
	}

	float peakLevels[ 8 ];
	float rmsLevels[ 8 ];

	XAUDIO2FX_VOLUMEMETER_LEVELS levels;
	levels.ChannelCount = outputChannels;
	levels.pPeakLevels = peakLevels;
	levels.pRMSLevels = rmsLevels;

	if ( levels.ChannelCount > 8 ) {
		levels.ChannelCount = 8;
	}

	pMasterVoice->GetEffectParameters( 0, &levels, sizeof( levels ) );

	int currentTime = Sys_Milliseconds();
	for ( int i = 0; i < outputChannels; i++ ) {
		if ( vuMeterPeakTimes[i] < currentTime ) {
			vuMeterPeak->SetValue( i, vuMeterPeak->GetValue( i ) * 0.9f, colorRed );
		}
	}

	float width = 20.0f;
	float height = 200.0f;
	float left = 100.0f;
	float top = 100.0f;

	sscanf( s_meterPosition.GetString(), "%f %f %f %f", &left, &top, &width, &height );

	vuMeterRMS->SetPosition( left, top, width * levels.ChannelCount, height );
	vuMeterPeak->SetPosition( left, top, width * levels.ChannelCount, height );

	for ( uint32 i = 0; i < levels.ChannelCount; i++ ) {
		vuMeterRMS->SetValue( i, rmsLevels[ i ], idVec4( 0.5f, 1.0f, 0.0f, 1.00f ) );
		if ( peakLevels[ i ] >= vuMeterPeak->GetValue( i ) ) {
			vuMeterPeak->SetValue( i, peakLevels[ i ], colorRed );
			vuMeterPeakTimes[i] = currentTime + s_meterTopTime.GetInteger();
		}
	}
}


/*
================================================
idSoundEngineCallback
================================================
*/

/*
========================
idSoundEngineCallback::OnCriticalError
========================
*/
void idSoundEngineCallback::OnCriticalError( HRESULT Error ) {
	soundSystemLocal.SetNeedsRestart();
}
