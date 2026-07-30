# macpad++ Documentation

**English** · [繁體中文](README.zh-TW.md)

Every document exists in both English and Traditional Chinese. English is the primary version at the
canonical path; the Chinese counterpart carries a `.zh-TW.md` suffix, and each links to the other at
the top of the page.

## Start here

| Document | What it is | 中文 |
|----------|------------|------|
| [../README.md](../README.md) | Project overview, feature list, download and installation | [中文](../README.zh-TW.md) |
| [../BUILD.md](../BUILD.md) | Building from source on macOS and Windows, packaging, troubleshooting | [中文](../BUILD.zh-TW.md) |
| [parity.md](parity.md) | Feature-by-feature comparison with Notepad++ v8.9.7, plus the honest list of what is not implemented | [中文](parity.zh-TW.md) |

## Architecture and development

| Document | What it is | 中文 |
|----------|------------|------|
| [design.md](design.md) | The full design document: layers, component and class diagrams, sequence diagrams, design decisions, per-sprint history | [中文](design.zh-TW.md) |
| [plugin-development.md](plugin-development.md) | How to write an extension: the protocol API, two worked examples, testing, pitfalls | [中文](plugin-development.zh-TW.md) |
| [parity-audit.md](parity-audit.md) | The 2026-07-08 parity audit (204 items), its later corrections, and the dual-platform re-assessment | [中文](parity-audit.zh-TW.md) |

## The Windows port (historical contracts)

The port is complete and merged; these documents are retained as the record of how it was specified.

| Document | What it is | 中文 |
|----------|------------|------|
| [plan.md](plan.md) | The porting plan: portability audit, phased execution, risks, effort estimate | [中文](plan.zh-TW.md) |
| [windows_prd.md](windows_prd.md) | Product requirements (PRD) with Given/When/Then acceptance criteria | [中文](windows_prd.zh-TW.md) |
| [windows_srs.md](windows_srs.md) | Software requirements specification (SRS) | [中文](windows_srs.zh-TW.md) |
| [windows_sa_sd.md](windows_sa_sd.md) | System architecture and design (SA/SD) | [中文](windows_sa_sd.zh-TW.md) |
| [windows_design.md](windows_design.md) | Per-file detailed design (the implementation blueprint) | [中文](windows_design.zh-TW.md) |

## Also in the repository

| File | What it is | 中文 |
|------|------------|------|
| [../THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) | Licences for the third-party assets shipped with the program | [中文](../THIRD-PARTY-NOTICES.zh-TW.md) |
| [../LICENSE](../LICENSE) | This project's own licence (MIT) | — |

---

## Contributing to the documentation

When you change a document, **update both language versions**. They are kept in step deliberately:
a reader in either language should get the same facts, not a summary and a full text.

- Links inside an English document point at English documents; links inside a Chinese document point at
  the `.zh-TW.md` counterparts.
- Keep the language switcher line directly under the title, with the current language in bold.
- Code identifiers, file paths, CLI flags and Mermaid diagram structure stay identical across both
  versions — only prose and diagram labels are translated.
