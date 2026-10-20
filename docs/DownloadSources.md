# Minecraft download sources

Open **Settings → Services → Minecraft Downloads** to choose **Official** (the default), **BMCLAPI**, or **Custom**. Custom accepts an HTTPS base URL for a server that implements BMCLAPI-compatible paths, such as `https://mirror.example.com/minecraft`. A path prefix is preserved. Credentials, query strings, fragments, and non-HTTPS URLs are rejected.

The BMCLAPI choice uses `https://bmclapi2.bangbang93.com`. OpenBMCLAPI is the distribution network behind that service; a node or control address is not a compatible launcher base URL by itself.

The mirror is used for these public Minecraft downloads:

- Game files from `piston-data.mojang.com` and `launcher.mojang.com` under `/v1/objects/`.
- Version and asset index JSON from `piston-meta.mojang.com` and `launchermeta.mojang.com` under `/v1/packages/`, plus the legacy asset index under `/mc/assets/legacy/`.
- Asset objects from `resources.download.minecraft.net`, mapped to the mirror's `/assets/` path.
- Mojang libraries from `libraries.minecraft.net`, mapped to the mirror's `/maven/` path.

If a mirror request fails or its checksum is wrong, the launcher retries the original official URL once. Existing checksums still validate the downloaded file. Other URLs, including authentication and account services, mod loaders, and third-party repositories, keep their original routing. The existing **Metadata Server** setting controls Prism metadata and the **Assets Server** setting takes precedence for asset objects; those settings continue to work independently of this selector.
