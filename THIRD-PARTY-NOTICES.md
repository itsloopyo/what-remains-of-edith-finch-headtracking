# Third-Party Notices

This mod bundles or links the following third-party components.

## Ultimate ASI Loader

- **Version:** v9.7.2 (commit `ab722befd52581a34449b603926cfab476e66b05`)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** the `winmm.dll` shim that loads our `.asi` plugin into
  `FinchGame.exe`.
- **Bundled:** yes. Shipped in the GitHub installer release ZIP under
  `vendor/ultimate-asi-loader/` and used as the install-time source.

Copyright (c) ThirteenAG

---

## MinHook

- **Version:** v1.3.3
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** runtime function hooking for the player view point trampoline.
- **Bundled:** yes. Fetched and built at compile time, statically linked into
  the shipped `.asi`; no separate file in the release ZIP.

Copyright (c) 2009-2017 Tsuda Kageyu. All rights reserved.

---

## OpenTrack (protocol)

- **Version:** n/a (wire protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** only the over-the-wire UDP protocol (48-byte `double[6]` packet on
  port 4242); no OpenTrack source is bundled.
- **Bundled:** no.

---

## CameraUnlock Core

- **Version:** commit `e1c29b7ca47ae936b198ed65c6e3ea77655d09d9`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** shared tracking pipeline, hook manager and Unreal runtime helpers.
- **Bundled:** yes. Consumed as a git submodule and statically linked into the
  shipped `.asi`.

Copyright (c) itsloopyo

---

## Credits

- Game by **Giant Sparrow**, published by **Annapurna Interactive**. Buy What
  Remains of Edith Finch on
  [Steam](https://store.steampowered.com/app/501300/). This mod does not
  contain or redistribute any game code or assets.
