#include "Eidophone.h"

/**
    The effect: the same plate with the clip printed on it, the sand on top.

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Eido Over` is twelve characters, inside FFGL's 16.
*/
namespace
{
class EidophoneEffect : public eido::EidophonePlugin
{
public:
	EidophoneEffect() :
		EidophonePlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< EidophoneEffect >,// Create method
	"ED02",                     // Plugin unique ID of maximum length 4
	"SW Eido Over",             // Plugin name
	2,                          // API major version number
	1,                          // API minor version number
	0,                          // Plugin major version number
	1,                          // Plugin minor version number
	FF_EFFECT,                  // Plugin type
	"Your clip printed on a Chladni plate, with sand on it, sounded by the music: the sand is thrown off the moving "
	"parts and settles on the nodal lines of whichever mode the note rings, so the figure changes with the note and "
	"holds through quiet passages.",
	"Eidophone FFGL effect"// About
);

extern "C" const char* EidophoneEffectBuildStamp()
{
	return "eidophone " EIDOPHONE_VERSION " effect, built " __DATE__ " " __TIME__;
}
