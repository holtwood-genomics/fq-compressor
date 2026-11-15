# htscodecs fqzcomp subset

This directory vendors only the quality codec needed by the experimental
`fqc-max/v1` container. It is not a general htscodecs build.

- Upstream: <https://github.com/samtools/htscodecs>
- Tag: `v1.6.7`
- Peeled commit: `b9fc194f772e45bb0a1f44b08cbf8697a1384bae`
- Tarball SHA-256: `4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a`
- License: BSD-3-Clause; see `LICENSE.md` and individual file headers.
- Vendored files: `fqzcomp_qual.c/h`, `utils.c/h`, `varint.h`,
  `c_simple_model.h`, `c_range_coder.h`, plus a minimal `config.h`.

The files are kept byte-identical except for the generated `config.h`; the
project builds them as a private C library (`fqc_fqzcomp`) and does not expose
the C API to installed headers.
