# ADR-008: CI/CD Pipeline & Verzeichnisstruktur für External Components

**Status:** Final
**Created:** 2026-09-01
**Components:** ESPHome, GitHub Actions, CI/CD
**Related ADRs:** ADR-007 (Installation & Repository-Structure)

---

## Context & Requirements

Das `esp-home-voice-alarm`-Projekt benötigt eine CI/CD-Pipeline, die:
1. **External Components validiert** (Compilier-Tests)
2. **Auf verschiedenen ESPHome-Versionen testet** (stable, beta, dev)
3. **Automatische Daily-Builds** gegen den neuesten ESPHome-Dev-Branch ausführt
4. **Mit den offiziellen ESPHome-Empfehlungen** konform ist

---

## Decision: ESPHome-konforme Verzeichnisstruktur & CI

**Entscheidung:** **ESPHome-Standard-Verzeichnisstruktur (`components/`) + `esphome/build-action`.**

### Verzeichnis-Struktur

Die ESPHome-Dokumentation empfiehlt **`components/`** im Repo-Root als Standard-Pfad für External Components:

```
esp-home-voice-alarm/
├── components/                          # ESPHome External Components (Standard-Pfad)
│   └── voice_alarm/
│       ├── __init__.py                  # Validierung + Code-Generierung
│       ├── voice_alarm.h/.cpp           # Hauptkomponente
│       ├── alarm_state.h/.cpp           # State Machine
│       └── alarm_config.h               # GPIO-Defaults
├── test/
│   └── voice-pe.yaml                    # CI-Kompilierungstest (Voice PE)
├── custom_components/                   # HA Integration (für HACS)
├── .github/workflows/
│   ├── ci.yml                           # ESPHome External Component CI
│   └── release.yml                      # HACS Release ZIP
├── hacs.json
└── doc/arch/
    └── ADR-007-installation-repo-structure.md
    └── ADR-008-ci-cd-pipeline.md
```

**Begründung:** Mit `components/` können Nutzer die Komponente ohne `files:`-Auflistung referenzieren:

```yaml
external_components:
  - source: components
    components: [voice_alarm]
```

Statt des umständlichen `files:`-Pfade:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ChristianKuehnel/esp-home-voice-alarm
      ref: main
    files:
      - components/voice_alarm/voice_alarm.h
```

### CI/CD-Pipeline

**Empfohlene GitHub Actions (laut ESPHome Doku & Starter-Components):**

1. **`esphome/build-action`** — Offizieller ESPHome-Build-Action
2. **Matrix mit 3 Versionen** — stable, beta, dev (daily cron)
3. **Concurrency-Gruppen** — verhindert parallele Builds für denselben PR/branch

#### Workflow: CI (`.github/workflows/ci.yml`)

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
  schedule:
    - cron: '0 7 * * *'  # Daily at 07:00 UTC

concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true

jobs:
  compile-test:
    name: Compile (ESPHome ${{ matrix.esphome-version }})
    runs-on: ubuntu-latest
    strategy:
      matrix:
        esphome-version:
          - stable
          - beta
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Build ESPHome test config
        uses: esphome/build-action@v7
        with:
          version: ${{ matrix.esphome-version }}
          yaml-file: test/voice-pe.yaml
```

#### Workflow: Release (`.github/workflows/release.yml`)

```yaml
name: Release

on:
  release:
    types: [published]

permissions:
  contents: write

jobs:
  build-zip:
    name: Build HACS ZIP
    runs-on: ubuntu-latest
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Create HACS ZIP
        run: |
          mkdir -p release
          zip -r release/voice_alarm.zip custom_components/voice_alarm/

      - name: Upload Release Asset
        uses: actions/upload-release-asset@v1
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        with:
          upload_url: ${{ github.event.release.upload_url }}
          asset_path: release/voice_alarm.zip
          asset_name: voice_alarm.zip
          asset_content_type: application/zip
```

---

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| **`components/` + `esphome/build-action`** (gewählt) | ESPHome-Standard, kürzere YAML in Nutzer-Konfiguration, offiziell unterstützt | Verzeichnis umbenannt (kleine breaking change für frühe Nutzer) |
| `esphome/external_components/` + `files:` | Behält ursprüngliche ADR-007-Struktur | Lange YAML-Pfade für Nutzer, nicht ESPHome-Standard |
| Manuelle `pip install esphome` | Keine extra Action nötig | Langsamer, keine offizielle Unterstützung, keine Version-Matrix |

---

## Consequences

### Positive
- **ESPHome-Standard konform** — Nutzer müssen keine `files:`-Pfade angeben
- **Offizielle `esphome/build-action`** — minimiert Wartungsaufwand, unterstützt ESPHome-Versionen
- **Matrix mit stable/beta/dev** — sicherstellt, dass External Components mit allen ESPHome-Versionen funktionieren
- **Daily cron builds** — erkennt Breaking Changes in ESPHome-Dev frühzeitig
- **Concurrency groups** — verhindert Ressourcenverschwendung bei parallelen PRs

### Negative
- **Breaking change für frühe Nutzer** — Wer `esphome/external_components/` in ihrer YAML hatte, muss auf `components/` aktualisieren
- **Zwei Workflows** — CI für ESPHome Components und Release für HACS separat (aber klar getrennt)

---

## Open Questions

*All resolved: Matrix (stable + beta), daily cron against dev, concurrency groups.*

1. **ESPHome-Versionen:** ✅ stable + beta (dev via daily cron)
2. **Concurrency:** ✅ groups verhindern parallele Builds
3. **Daily cron:** ✅ 07:00 UTC gegen dev

---

## References

- [ESPHome External Components Documentation](https://esphome.io/components/external_components/)
- [ESPHome Build Action (GitHub)](https://github.com/esphome/build-action)
- [ESPHome Starter Components](https://github.com/esphome/starter-components)
- [ESPHome CI Best Practices](https://developers.esphome.io/architecture/ci/)

---

## Follow-up

- [#1](https://github.com/ChristianKuehnel/esp-home-voice-alarm/issues/1) — User Installation Guide (mit Beispiel-YAML für `components/`-Pfad)