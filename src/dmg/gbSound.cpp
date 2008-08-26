// VisualBoyAdvance - Nintendo Gameboy/GameboyAdvance (TM) emulator.
// Copyright (C) 1999-2003 Forgotten
// Copyright (C) 2005-2006 Forgotten and the VBA development team
// Copyright (C) 2007-2008 VBA-M development team
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or(at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include <memory.h>

#include "../System.h"
#include "../Util.h"
#include "gbGlobals.h"
#include "gbSound.h"

#include "gb_apu/Gb_Apu.h"
#include "gb_apu/Effects_Buffer.h"

extern int soundGetEnable();

static Gb_Apu* gb_apu;
static Simple_Effects_Buffer* stereo_buffer;

extern u16 soundFinalWave[1470];

extern int gbHardware;

extern void soundResume();

extern int soundBufferLen;
extern int soundQuality;
extern bool soundPaused;
extern int soundTicks;
extern int SOUND_CLOCK_TICKS;

int const ticks_to_time = 2 * GB_APU_OVERCLOCK;

static inline blip_time_t blip_time()
{
	return (SOUND_CLOCK_TICKS - soundTicks) * ticks_to_time;
}

u8 gbSoundRead( u16 address )
{
	if ( gb_apu && address >= NR10 && address <= 0xFF3F )
		return gb_apu->read_register( blip_time(), address );

	return gbMemory[address];
}

void gbSoundEvent(register u16 address, register int data)
{
	int freq = 0;

	gbMemory[address] = data;

	if ( gb_apu && address >= NR10 && address <= 0xFF3F )
		gb_apu->write_register( blip_time(), address, data );
}

static void end_frame( blip_time_t time )
{
	gb_apu       ->end_frame( time );
	stereo_buffer->end_frame( time );
}

static void flush_samples()
{
	// number of samples in output buffer
	int const out_buf_size = soundBufferLen / sizeof *soundFinalWave;

	// Keep filling and writing soundFinalWave until it can't be fully filled
	while ( stereo_buffer->samples_avail() >= out_buf_size )
	{
		stereo_buffer->read_samples( (blip_sample_t*) soundFinalWave, out_buf_size );
		if(soundPaused)
			soundResume();

		systemWriteDataToSoundBuffer();
	}
}

int const chan_count = 4;

gb_effects_config_t gb_effects_config;
static gb_effects_config_t gb_effects_config_current;

static int prevSoundEnable = -1;

static void apply_effects()
{
	prevSoundEnable = soundGetEnable();
	gb_effects_config_current = gb_effects_config;

	stereo_buffer->config().enabled  = gb_effects_config_current.enabled;
	stereo_buffer->config().echo     = gb_effects_config_current.echo;
	stereo_buffer->config().stereo   = gb_effects_config_current.stereo;
	stereo_buffer->config().surround = gb_effects_config_current.surround;
	stereo_buffer->apply_config();

	for ( int i = 0; i < chan_count; i++ )
	{
		Multi_Buffer::channel_t ch = { 0, 0, 0 };
		if ( prevSoundEnable >> i & 1 )
			ch = stereo_buffer->channel( i );
		gb_apu->set_output( ch.center, ch.left, ch.right, i );
	}
}

void gbSoundConfigEffects( gb_effects_config_t const& c )
{
	gb_effects_config = c;
}

void gbSoundTick()
{
 	if ( gb_apu && stereo_buffer )
	{
		// Run sound hardware to present
		end_frame( SOUND_CLOCK_TICKS * ticks_to_time );

		flush_samples();

		// Update effects config if it was changed
		if ( memcmp( &gb_effects_config_current, &gb_effects_config,
				sizeof gb_effects_config ) || soundGetEnable() != prevSoundEnable )
			apply_effects();
	}
}

static void reset_apu()
{
	Gb_Apu::mode_t mode = Gb_Apu::mode_dmg;
	if ( gbHardware & 1 )
		mode = Gb_Apu::mode_cgb;
	if ( gbHardware & 4 )
		mode = Gb_Apu::mode_agb;
	gb_apu->reset( mode );

	if ( stereo_buffer )
		stereo_buffer->clear();

	soundTicks = SOUND_CLOCK_TICKS;
}

static void remake_stereo_buffer()
{
	// Stereo_Buffer
	delete stereo_buffer;
	stereo_buffer = 0;

	stereo_buffer = new Simple_Effects_Buffer; // TODO: handle out of memory
	stereo_buffer->set_sample_rate( 44100 / soundQuality ); // TODO: handle out of memory
	stereo_buffer->clock_rate( gb_apu->clock_rate );

	// APU
	static int const chan_types [chan_count] = {
		Multi_Buffer::wave_type+1, Multi_Buffer::wave_type+2,
		Multi_Buffer::wave_type+3, Multi_Buffer::mixed_type+1
	};
	stereo_buffer->set_channel_count( chan_count, chan_types );

	if ( !gb_apu )
	{
		gb_apu = new Gb_Apu;
		reset_apu();
	}

	apply_effects();
}

void gbSoundReset()
{
	gb_effects_config.echo     = 0.20f;
	gb_effects_config.stereo   = 0.15f;
	gb_effects_config.surround = false;

	SOUND_CLOCK_TICKS = 20000;

	remake_stereo_buffer();
	reset_apu();

	soundPaused       = 1;

	gbSoundEvent(0xff10, 0x80);
	gbSoundEvent(0xff11, 0xbf);
	gbSoundEvent(0xff12, 0xf3);
	gbSoundEvent(0xff14, 0xbf);
	gbSoundEvent(0xff16, 0x3f);
	gbSoundEvent(0xff17, 0x00);
	gbSoundEvent(0xff19, 0xbf);

	gbSoundEvent(0xff1a, 0x7f);
	gbSoundEvent(0xff1b, 0xff);
	gbSoundEvent(0xff1c, 0xbf);
	gbSoundEvent(0xff1e, 0xbf);

	gbSoundEvent(0xff20, 0xff);
	gbSoundEvent(0xff21, 0x00);
	gbSoundEvent(0xff22, 0x00);
	gbSoundEvent(0xff23, 0xbf);
	gbSoundEvent(0xff24, 0x77);
	gbSoundEvent(0xff25, 0xf3);

	if (gbHardware & 0x4)
		gbSoundEvent(0xff26, 0xf0);
	else
		gbSoundEvent(0xff26, 0xf1);

	int addr = 0xff30;

	while(addr < 0xff40) {
		gbMemory[addr++] = 0x00;
		gbMemory[addr++] = 0xff;
	}
}

extern bool soundInit();
extern void soundShutdown();

void gbSoundSetQuality(int quality)
{
	if ( soundQuality != quality )
	{
		if ( systemCanChangeSoundQuality() )
		{
			soundShutdown();
			soundQuality      = quality;
			soundInit();
		}
		else
		{
			soundQuality      = quality;
		}

		remake_stereo_buffer();
	}
}

static struct {
	int version;
	gb_apu_state_t apu;
} state;

static char dummy_state [735 * 2];

#define SKIP( type, name ) { dummy_state, sizeof (type) }

#define LOAD( type, name ) { &name, sizeof (type) }

// Old save state support

static variable_desc gbsound_format [] =
{
	SKIP( int, soundPaused ),
	SKIP( int, soundPlay ),
	SKIP( int, soundTicks ),
	SKIP( int, SOUND_CLOCK_TICKS ),
	SKIP( int, soundLevel1 ),
	SKIP( int, soundLevel2 ),
	SKIP( int, soundBalance ),
	SKIP( int, soundMasterOn ),
	SKIP( int, soundIndex ),
	SKIP( int, soundVIN ),
	SKIP( int, soundOn [0] ),
	SKIP( int, soundATL [0] ),
	SKIP( int, sound1Skip ),
	SKIP( int, soundIndex [0] ),
	SKIP( int, sound1Continue ),
	SKIP( int, soundEnvelopeVolume [0] ),
	SKIP( int, soundEnvelopeATL [0] ),
	SKIP( int, sound1EnvelopeATLReload ),
	SKIP( int, sound1EnvelopeUpDown ),
	SKIP( int, sound1SweepATL ),
	SKIP( int, sound1SweepATLReload ),
	SKIP( int, sound1SweepSteps ),
	SKIP( int, sound1SweepUpDown ),
	SKIP( int, sound1SweepStep ),
	SKIP( int, soundOn [1] ),
	SKIP( int, soundATL [1] ),
	SKIP( int, sound2Skip ),
	SKIP( int, soundIndex [1] ),
	SKIP( int, sound2Continue ),
	SKIP( int, soundEnvelopeVolume [1] ),
	SKIP( int, soundEnvelopeATL [1] ),
	SKIP( int, sound2EnvelopeATLReload ),
	SKIP( int, sound2EnvelopeUpDown ),
	SKIP( int, soundOn [2] ),
	SKIP( int, soundATL [2] ),
	SKIP( int, sound3Skip ),
	SKIP( int, soundIndex [2] ),
	SKIP( int, sound3Continue ),
	SKIP( int, sound3OutputLevel ),
	SKIP( int, soundOn [3] ),
	SKIP( int, soundATL [3] ),
	SKIP( int, sound4Skip ),
	SKIP( int, soundIndex [3] ),
	SKIP( int, sound4Clock ),
	SKIP( int, sound4ShiftRight ),
	SKIP( int, sound4ShiftSkip ),
	SKIP( int, sound4ShiftIndex ),
	SKIP( int, sound4NSteps ),
	SKIP( int, sound4CountDown ),
	SKIP( int, sound4Continue ),
	SKIP( int, soundEnvelopeVolume [2] ),
	SKIP( int, soundEnvelopeATL [2] ),
	SKIP( int, sound4EnvelopeATLReload ),
	SKIP( int, sound4EnvelopeUpDown ),
	SKIP( int, soundEnableFlag ),
	{ NULL, 0 }
};

static variable_desc gbsound_format2 [] =
{
	SKIP( int, sound1ATLreload ),
	SKIP( int, freq1low ),
	SKIP( int, freq1high ),
	SKIP( int, sound2ATLreload ),
	SKIP( int, freq2low ),
	SKIP( int, freq2high ),
	SKIP( int, sound3ATLreload ),
	SKIP( int, freq3low ),
	SKIP( int, freq3high ),
	SKIP( int, sound4ATLreload ),
	SKIP( int, freq4 ),
	{ NULL, 0 }
};

static variable_desc gbsound_format3 [] =
{
	SKIP( u8[2*735], soundBuffer ),
	SKIP( u8[2*735], soundBuffer ),
	SKIP( u16[735], soundFinalWave ),
	{ NULL, 0 }
};

enum {
	nr10 = 0,
	nr11, nr12, nr13, nr14,
	nr20, nr21, nr22, nr23, nr24,
	nr30, nr31, nr32, nr33, nr34,
	nr40, nr41, nr42, nr43, nr44,
	nr50, nr51, nr52
};

static void gbSoundReadGameOld(int version,gzFile gzFile)
{
	if ( version == 11 )
	{
		// Version 11 didn't save any state
		// TODO: same for version 10?
		state.apu.regs [nr50] = 0x77; // volume at max
		state.apu.regs [nr51] = 0xFF; // channels enabled
		state.apu.regs [nr52] = 0x80; // power on
		return;
	}
	
	// Load state
	utilReadData( gzFile, gbsound_format );

	if ( version >= 11 )
		utilReadData( gzFile, gbsound_format2 );

	utilReadData( gzFile, gbsound_format3 );

	int quality = 1;
	if ( version >= 7 )
		quality = utilReadInt( gzFile );

	gbSoundSetQuality( quality );

	// Convert to format Gb_Apu uses
	gb_apu_state_t& s = state.apu;

	// Only some registers are properly preserved
	static int const regs_to_copy [] = {
		nr10, nr11, nr12, nr21, nr22, nr30, nr32, nr42, nr43, nr50, nr51, nr52, -1
	};
	for ( int i = 0; regs_to_copy [i] >= 0; i++ )
		s.regs [regs_to_copy [i]] = gbMemory [0xFF10 + regs_to_copy [i]];

	memcpy( &s.regs [0x20], &gbMemory [0xFF30], 0x10 ); // wave
}

// New state format

static variable_desc gb_state [] =
{
	LOAD( int, state.version ),				// room_for_expansion will be used by later versions
	
	// APU
	LOAD( u8 [0x40], state.apu.regs ),      // last values written to registers and wave RAM (both banks)
	LOAD( int, state.apu.frame_time ),      // clocks until next frame sequencer action
	LOAD( int, state.apu.frame_phase ),     // next step frame sequencer will run

	LOAD( int, state.apu.sweep_freq ),      // sweep's internal frequency register
	LOAD( int, state.apu.sweep_delay ),     // clocks until next sweep action
	LOAD( int, state.apu.sweep_enabled ),
	LOAD( int, state.apu.sweep_neg ),       // obscure internal flag
	LOAD( int, state.apu.noise_divider ),
	LOAD( int, state.apu.wave_buf ),        // last read byte of wave RAM

	LOAD( int [4], state.apu.delay ),       // clocks until next channel action
	LOAD( int [4], state.apu.length_ctr ),
	LOAD( int [4], state.apu.phase ),       // square/wave phase, noise LFSR
	LOAD( int [4], state.apu.enabled ),     // internal enabled flag

	LOAD( int [3], state.apu.env_delay ),   // clocks until next envelope action
	LOAD( int [3], state.apu.env_volume ),
	LOAD( int [3], state.apu.env_enabled ),

	SKIP( int [13], room_for_expansion ),

	// Emulator
	SKIP( int [16], room_for_expansion ),

	{ NULL, 0 }
};

void gbSoundSaveGame( gzFile out )
{
	gb_apu->save_state( &state.apu );

	// Be sure areas for expansion get written as zero
	memset( dummy_state, 0, sizeof dummy_state );

	state.version = 1;
	utilWriteData( out, gb_state );
}

void gbSoundReadGame( int version, gzFile in )
{
	// Prepare APU and default state
	reset_apu();
	gb_apu->save_state( &state.apu );
	
	if ( version > 11 )
		utilReadData( in, gb_state );
	else
		gbSoundReadGameOld( version, in );

	gb_apu->load_state( state.apu );
}
