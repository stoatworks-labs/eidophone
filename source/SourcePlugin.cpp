#include "Eidophone.h"

/**
    The source: a metal plate seen from above, strewn with sand, pushed at its
    centre by the music.

    Listed directly in the EidophoneSource target, not in eidophone_core: both
    plugins share the class and not the `CFFGLPluginInfo` below, and putting
    either registration in the shared library would register both plugins into
    both bundles. The core is an OBJECT library because this registers itself
    from a file-scope constructor nothing references.

    `SW Eidophone` is twelve characters; the FFGL name field is char[ 16 ] and
    not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class EidophoneSource : public eido::EidophonePlugin
{
public:
	EidophoneSource() :
		EidophonePlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< EidophoneSource >,// Create method
	"ED01",                     // Plugin unique ID of maximum length 4
	"SW Eidophone",             // Plugin name
	2,                          // API major version number
	1,                          // API minor version number
	0,                          // Plugin major version number
	1,                          // Plugin minor version number
	FF_SOURCE,                  // Plugin type
	"A Chladni plate with sand on it, sounded by the music. The detected pitch rings the plate's nearest modes "
	"(Kirchhoff plate, free edges); sand is thrown only where the plate's acceleration beats gravity and settles on the "
	"nodal lines, so the figure changes with the note and freezes in quiet passages. Powder gathers the other way.",
	"Eidophone FFGL source"// About
);

extern "C" const char* EidophoneSourceBuildStamp()
{
	return "eidophone " EIDOPHONE_VERSION " source, built " __DATE__ " " __TIME__;
}
