/*
 * Stoatworks Labs - About window data for Eidophone.
 *
 * PROVISIONAL HAND COPY, adapted from haze's on 2026-11-16. Eidophone is not
 * registered with stoatworks-backend yet (no projects.json card, no guide), so
 * sync-about.py has never generated this file: `guide` is empty and the About
 * block shows no guide button. The next sync, once the project is registered,
 * overwrites it.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Eidophone";
    inline constexpr auto slug = "eidophone";
    inline constexpr auto hook = "A Chladni plate with sand on it, sounded by the music, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/eidophone/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/eidophone";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
