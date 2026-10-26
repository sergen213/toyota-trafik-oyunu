# Legal and asset-handling policy

- The game, its executables, artwork, models, audio, video and texts are © ToyotaSA / Martı Animasyon. No
  redistribution licence is known; the CD was given away free, which does **not** grant redistribution rights.
- This repository contains **only project-written code, documentation and derived data**. It never contains
  the original executables, DLLs, MSI/CAB files, images, meshes, audio, video, `SoruBank` text or
  `UserData.dat`. `.gitignore` excludes the CD image folder, the RAR and the private `work/` area.
- `data/maps.json` holds numeric coordinates and file-name references recovered from the executable's map
  builders (facts needed for interoperability); it contains no artwork or text content of the game.
- Users must supply their own copy of the game (CD or installed folder). The native program reads it in place
  and never modifies, uploads or downloads game files.
- Reverse engineering was performed on a legitimately obtained copy for preservation and interoperability.
  No DRM, activation or licence check exists in the game (see `FAILURE_ANALYSIS.md`), so none was bypassed.
- Third-party code: ODE (BSD, `third_party/ode-LICENSE-BSD.TXT`) when built from the pinned tarball; system
  libraries (SDL3, SDL3_ttf, GLEW, mpg123, stb, GoogleTest) under their own licences.
- Before any public release, re-run the tracked-file check in `TESTING.md` and keep releases free of game data.
