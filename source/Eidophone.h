#pragma once

#include "Audio.h"
#include "Controls.h"
#include "Modes.h"
#include "PassBuffer.h"
#include "Plate.h"
#include "Shaders.h"

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace eido
{

/// The modal state set directly, so a check about the grains is not also a
/// check about the audio. Never set by a host.
struct TestDrive
{
	bool pure     = false;
	int mode      = 0;  ///< index into the table (0, 1 the rigid modes)
	double accel  = 0.0;///< a_k, m/s^2 (real, so the field is a real standing wave)
	int mode2     = -1; ///< an optional second mode
	double accel2 = 0.0;
	double accel2Imag = 0.0;///< its imaginary part: a field whose |A| is not |Re A|
	double omega  = 2.0 * kPi * 200.0;///< rad/s, for the launch speed and the powder current
};

/// What one frame ran with: the harness builds its closed forms from it.
struct FrameData
{
	Drive drive;
	double dt          = 0.0;
	double now         = 0.0;
	int substeps       = 0;
	uint32_t firstStep = 0;///< the Step uniform of this frame's first substep
	int grains         = 0;
	double halfSize    = 0.15;///< L, m
	double omega       = 0.0; ///< the grains' w_d, rad/s
	double plateScale  = 1.0; ///< px per plate unit
	double pointSize   = 1.0;
	bool poured        = false;
	double peakPerNewton = 0.0;///< Plate::PeakPerNewton at the drive's frequency
	std::vector< std::complex< float > > coef;///< a_k as uploaded
};

/**
    The plugin: the source (`SW Eidophone`, the plate on black) and, with
    `isEffect`, the Over (`SW Eido Over`, the clip printed on the plate). One
    class, two registrations; see SourcePlugin.cpp and EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class EidophonePlugin : public CFFGLPlugin
{
public:
	explicit EidophonePlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	/// The host's audio engine rate: the axis of its FFT buffer (earshot's
	/// AGENTS.md, "Resolume's 64 bins, measured"). Arena sends 48000.
	void SetSampleRate( unsigned int rate ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// The mode table for a shape and Poisson's ratio: built once per process
	/// and shared (a square ~70 ms, a circle ~230 ms, here).
	static std::shared_ptr< const modes::Table > TableFor( Shape shape, double nu );

	//The harness's hooks. A host never calls these.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int bits )
	{
		hooks       = bits;
		plate.hooks = bits & 15;
		audio.hooks = bits;
	}
	void SetTestDrive( const TestDrive& drive )
	{
		testDrive = drive;
	}
	/// Exactly n substeps on each following frame; -1 gives the clock back.
	void SetSubstepsForTest( int n )
	{
		forcedSubsteps = n;
	}
	/// Pour again on the next frame.
	void PourForTest()
	{
		pourPending = true;
	}
	AudioInput& Audio()
	{
		return audio;
	}
	const Plate& PlateState() const
	{
		return plate;
	}
	const modes::Table* ModeTable() const
	{
		return table.get();
	}
	const FrameData& LastFrame() const
	{
		return frame;
	}
	double HostNow() const
	{
		return now;
	}
	GLuint FieldFBO() const
	{
		return field.GetGLID();
	}
	GLuint StateFBO() const
	{
		return state[ current ].GetGLID();
	}
	GLuint StateTextureID() const
	{
		return state[ current ].TextureID();
	}
	GLuint DensityFBO() const
	{
		return density.GetGLID();
	}
	uint32_t StepCounter() const
	{
		return stepCounter;
	}

private:
	void UpdateClock();
	void ensureTable();
	void uploadModes();
	void drawField();
	void drawPour( Shape shape, Grain grain );
	void drawUpdate( int rows, int count );
	void drawSplat( int width, int height, int count );
	void drawFinal( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );
	void refreshDeviceMenu();

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, unsigned int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader fieldShader, pourShader, updateShader, splatShader, finalShader;
	ffglex::FFGLScreenQuad quad;
	GLuint modeTexture = 0;///< GL_TEXTURE_2D_ARRAY, R32F, kFieldSize^2 x kMaxModes
	GLuint emptyVAO    = 0;
	PassBuffer field;      ///< RG32F kFieldSize^2
	PassBuffer state[ 2 ]; ///< RGBA32F kStateSize^2, ping-pong
	PassBuffer density;    ///< RG16F at the output raster
	int current = 0;
	const modes::Table* uploaded = nullptr;
	bool needPour = true;

	std::shared_ptr< const modes::Table > table;
	Shape tableShape    = Shape::Count;
	Material tableMaterial = Material::Count;
	Plate plate;
	AudioInput audio;
	FrameData frame;
	TestDrive testDrive;
	int forcedSubsteps = -1;
	double substepClock = 0.0;
	uint32_t stepCounter = 0;
	uint32_t pourSeed    = 0;
	int lastGrain = -1, lastShape = -1;

	//The clock (double throughout: Resolume's has been seen at ~499 million ms).
	double hostTime     = -1.0;
	double now          = 0.0;
	double lastNow      = -1.0;
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	bool settledJump = false;

	bool pourHeld    = false;
	bool pourPending = false;
	bool rescanHeld  = false;
	int hooks        = 0;
	/// SetSampleRate's, 0 until the host sends one. Atomic: a host may call
	/// SetSampleRate from another thread than the frame's.
	std::atomic< unsigned int > hostRate { 0 };
};

} // namespace eido
