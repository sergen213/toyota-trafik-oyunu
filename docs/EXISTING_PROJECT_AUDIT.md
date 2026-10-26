# Audit of existing preservation projects

All directories were inspected read-only (directory listing, README, git HEAD). None contains Toyota Trafik
Oyunu files. The game files exist only in `/mnt/2nd4TB/ToyotaTrafikOyunu/` (CD image folder + identical RAR).

| Directory | What it is (from its README / git) | Relevance |
|---|---|---|
| `/mnt/1st4TB/Kabus 22` | Native Linux engine for Kabus 22 (Acknex/A6 WDL game), C++20, SDL2, FFmpeg, ODE physics, GTK installer, signed updater; HEAD `9625182 release: 0.4.8` | Same "native reimplementation reading user-owned data" approach; its ODE use confirms ODE is a workable physics choice, but the code is engine-specific. No assets reused. |
| `/mnt/1st4TB/etiyami` | ETI Yami native C++20/SDL3 reconstruction (OpenGL/Vulkan, FFmpeg video, launcher, updates); HEAD `2e31fa9` | Confirms SDL3 + OpenGL stack and "assets not included, legitimate copy required" release policy used here. |
| `/mnt/1st4TB/Pusu: Uyanış` | Native Linux implementation of Pusu: Uyanış (OpenGL + Vulkan RT, GTK installer); HEAD `c17913b` | Same legal/release policy; nothing GLScene-related. |
| `/mnt/2nd4TB/IKV` | İstanbul Kıyamet Vakti native client/server, Qt6 launcher, C++20, SDL3; HEAD `63bc8ef` | Online game; unrelated formats. |
| `/mnt/2nd4TB/CitroenC4Robot` | Citroën C4 Robot native recovery (C++20, Qt6 launcher, updater, tests) | Another Turkish-market branded promotional game; similar distribution history but different engine. |
| `/mnt/2nd4TB/SuperCan` | SuperCan recovery project (C++/tools/tests, analysis) | Different game; no shared formats found. |
| `/mnt/1st4TB/BugsBunny` | PS1 Bugs Bunny Lost in Time / Time Busters research, Ghidra workspace | Unrelated platform. |

Reused from these projects: only working practices (private `work/` area for Wine prefixes and captures,
Xvfb-isolated runtime probing, "bring your own data" packaging). No code or data was copied, and none of
these directories were modified.
