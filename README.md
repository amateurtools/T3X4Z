first draft of the R34DM3: 31 October 2026

T3X4Z is pronounced 'TEXAS'

Texas is, phonetically, a trademark of Texas, USA.

T3X4Z is an emulation of the hardware inside a Yamaha TX81Z-class FM synthesizer (HD6303X CPU, YM2414 sound chip, LCD and mostly dissimilar front panel).
It is an independent project and is not affiliated with, endorsed by, or licensed by Yamaha.
“TX81Z” and “Yamaha” are trademarks of their owner and are used here only to describe compatibility.
No firmware is included. The plugin runs the original TX81Z firmware, which is Yamaha’s copyrighted software and cannot be redistributed with this project.

You need to supply your own ROM image (for example, one you dumped from a unit you own).

Load the ROM from the plugin, set the TX81Z_ROM environment variable, or place it at ~/.config/TX81ZNative/rom.ic15.
The plugin will remember the path of the rom, so if you move the rom the plugin won't start until you locate it again. (as of 31 october 2026.)

Credits: ymfm (BSD-3, Aaron Giles) and the memory map taken from MAME’s ymtx81z.cpp (BSD-3, AJR) both require their copyright notices and licence text to stay with the source. 

This is 100% organically vibecoded by Claude.ai to exacting specifications that were artisanally crafted by The Actual Culprit: Amateurtools DSP.

CREDITS:
YMFM: https://github.com/aaronsgiles/ymfm/blob/main/LICENSE
Claude: Vibe
AmateurTools: Concept

Build and implementation credits include:
JUCE: https://juce.com/legal/juce-7-license/
VST3: https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/VST3+License.html
CLAP: https://github.com/microsoft/CLAP/blob/main/LICENSE
