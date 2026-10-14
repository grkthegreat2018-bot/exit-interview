EXIT INTERVIEW
==============

A horror spot-the-anomaly game. You work the night shift at an office
front desk: interview visitors, watch for things that shouldn't be
there, and file reports before the building gets unstable.

RUN
---
Double-click exit_interview.exe  (keep the assets folder next to it)

If Windows says a DLL like VCRUNTIME140.dll is missing, install the
free "Visual C++ Redistributable (x64)" from Microsoft:
https://aka.ms/vs/17/release/vc_redist.x64.exe

Controls are listed/rebindable in Pause -> Settings -> CONTROLS.

NOTE
----
The game runs fully offline with a built-in stub AI. If you have a
Groq API key you can set the GROQ_API_KEY environment variable to
enable live LLM dialogue, otherwise it just works without it.
