Security Notes
- `gorgon-aead` is recommended for all new archives.
- Use `-p -`, `-p @file`, or `-p env:NAME` instead of typing passwords directly on the command line.
- v3.0.0 rejects malformed archives or archives declaring sizes above the configured limits earlier, before allocation.
- Legacy CBC mode remains available for compatibility, but it is no longer the default for new archives.
