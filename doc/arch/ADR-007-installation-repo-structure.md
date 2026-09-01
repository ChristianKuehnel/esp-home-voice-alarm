# ADR-007: Installation & Repository-Structure

**Status:** Final
**Created:** 2026-09-01
**Components:** ESPHome, Home Assistant, HACS
**Related PRDs:** NFR-9 (HA/ESPHome Standards Compliance), FR-12 (HACS Python Integration)

---

## Context & Requirements

Das `esp-home-voice-alarm`-Projekt besteht aus zwei verteilbaren Software-Komponenten:

1. **ESPHome External Component** — C++-Firmware-Komponente für ESP32-Geräte (Display, Buzzer, Sensor-Logik, State Machine)
2. **Home Assistant Custom Integration** — Python-Integration in HA (Alarm-Entitäten, Service-Registrations, Entity-Management)

Beide müssen **einfach installierbar** sein:
- ESPHome-Komponente als **External Component** (direkt aus dem GitHub-Repo)
- HA-Integration über **HACS** (Community-Standard für Custom Integrations)

Die Repository-Struktur muss beide Komponenten in **einem Repo** unterstützen, ohne Konflikte.

---

## Decision: Repository-Struktur & Installation

**Entscheidung:** **Option 1 (Ein Repo mit strukturierter Verzeichnis-Hierarchie).**

### Gewählte Struktur

```
esp-home-voice-alarm/
├── README.md
├── LICENSE
├── hacs.json                          # HACS-Manifest (für HACS-Erkennung)
├── doc/
│   ├── prd.md
│   └── arch/
│       ├── ADR-001-state-machine.md
│       ├── ADR-002-nvs-layout.md
│       ├── ADR-003-intent-payload.md
│       ├── ADR-004-physical-button.md
│       ├── ADR-005-alarm-volume.md
│       ├── ADR-006-voice-commands.md
│       └── ADR-007-installation-repo-structure.md
├── esphome/
│   └── external_components/
│       └── voice_alarm/
│           ├── voice_alarm.h          # Haupt-Header (ESPHome-namespace)
│           ├── voice_alarm.cpp        # Implementierung
│           ├── alarm_state.h          # State-Header
│           ├── alarm_state.cpp        # State-Logik
│           └── alarm_config.h         # Config/GPIO-Definitionen
└── custom_components/
    └── voice_alarm/
        ├── __init__.py                # Integration setup, services
        ├── voice_alarm.py             # Alarm entity
        ├── number.py                  # Volume number entity
        ├── manifest.json              # HACS/HA manifest
        ├── translations/de.json       # Deutsche Übersetzungen
        └── config_flow.py             # HA Config Flow
```

### Warum ein Repo?

| Faktor | Ein Repo | Zwei Repos |
|--------|----------|------------|
| Versionierung | ESP32 FW + HA Integration immer synchron | Versions-Divergenz riskant |
| Warten | Single PR für beide Seiten | Doppelte Reviews |
| HACS | Kann `zip_file` oder Root-Pfad nutzen | Extra Repo nur für HA |
| ESPHome External | `github://owner/repo@main/esphome/...` | Kein Problem |

### ESPHome External Component Installation

Nutzer fügt dies in ihre ESPHome YAML ein:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ChristianKuehnel/esp-home-voice-alarm
      ref: main
    files:
      - esphome/external_components/voice_alarm/voice_alarm.h
```

ESPHome erkennt automatisch den `esphome/`-Pfad innerhalb des Repos, wenn die Komponente im `components/`- oder `esphome/components/`-Verzeichnis liegt. Alternativ: explizite `files:`-Angabe.

**Referenzen:**
- [ESPHome External Components Documentation](https://esphome.io/components/external_components/)
- [ESPHome Blog: Removal of Custom Components (External Components sind die Nachfolger)](https://developers.esphome.io/blog/2025/02/19/about-the-removal-of-support-for-custom-components/)

### HACS Installation (zip_file)

**Entscheidung:** HACS installiert eine **`voice_alarm.zip`** als GitHub Release-Asset.
Nicht der gesamte Repo-Root, sondern nur der `custom_components/`-Teil wird als ZIP ausgeliefert.
Das filtert ESPHome-Code aus HACS heraus und sorgt für saubere Trennung.

**Installationsweg (für Endnutzer):**

1. HACS → **Custom Repositories** → `ChristianKuehnel/esp-home-voice-alarm` → Kategorie **Integration**
2. Suche nach **"Alarm Clock"** → Installieren
3. HA neu starten

`hacs.json` im Repository-Root konfiguriert HACS (`filename` verweist auf Release-Asset):

```json
{
  "name": "Alarm Clock",
  "render_readme": true,
  "homeassistant": "2024.1.0",
  "filename": "voice_alarm.zip"
}
```

`custom_components/voice_alarm/manifest.json`:

```json
{
  "domain": "voice_alarm",
  "name": "Voice Alarm Clock",
  "documentation": "https://github.com/ChristianKuehnel/esp-home-voice-alarm",
  "issue_tracker": "https://github.com/ChristianKuehnel/esp-home-voice-alarm/issues",
  "requirements": [],
  "dependencies": [],
  "codeowners": ["@ChristianKuehnel"],
  "iot_class": "local_polling",
  "version": "0.1.0"
}
```

**Referenzen:**
- [HACS Custom Repository Documentation](https://hacs.xyz/docs/comparing/custom)
- [HACS Installation Guide](https://hacs.xyz/docs/components/hacs/installation)
- [Home Assistant Custom Component Structure](https://developers.home-assistant.io/docs/create_platform_index)

---

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| **Ein Repo (gewählt)** | Versionen synchron, single source of truth, einfacher Link für beide Nutzergruppen | HACS-Root enthält ESPHome-Code (muss durch `hacs.json` gefiltert werden) |
| Zwei getrennte Repos | Klare Trennung, HACS Repo nur mit Python-Code | Versions-Divergenz, doppelte Wartung, zwei Links für Nutzer |
| `zip_file` in HACS | Kein HACS nötig, direkter Download | Keine Auto-Updates, manuelle Pflege nötig |
| ESPHome Package (yaml-only) | Keine C++-Komponente nötig, einfacher | Kein Zugang zu State-Machine/RTC-Hardware (C++ erforderlich) |

---

## Consequences

### Positive
- **Ein Repo** = einfach zu verlinken, einzufachen zu versionieren, doppelte Arbeit vermieden
- **ESPHome External Components** = keine lokale C++-Entwicklung nötig, direkte Git-Integration
- **HACS** = Standard-Weg für HA-Custom-Integrations, Auto-Updates möglich
- **Klare Verzeichnis-Trennung** (`esphome/` vs `custom_components/`) = keine Konflikte

### Negative
- HACS versucht möglicherweise, den gesamten Repo-Inhalt zu scannen (muss durch `hacs.json` + korrektes Manifest gefiltert werden)
- ESPHome External Components laden den ganzen Repo-Tree via Git (klein enough, aber kein submodule-only-Patch)
- Neue Nutzer müssen verstehen, dass **beide** Installationen nötig sind (ESPHome + HA)

---

## Open Questions

*All resolved: zip_file, `voice_alarm` domain & filename, semantic tagging, CI pipeline.*

1. **HACS `zip_file`:** ✅ GitHub Release-Asset `voice_alarm.zip` enthält nur `custom_components/voice_alarm/`. CI zippt den HA-Teil via GitHub Actions bei Tags.
2. **Domain-Name:** ✅ `voice_alarm` — verhindert Kollisionen mit anderen "alarm_clock"-Integrations im HACS-Ökosystem. Vollständiger Name: "Voice Alarm Clock".
3. **Tagging-Policy:** ✅ `main`-Branch + semantische Tags (`v0.1.0`, `v0.2.0`) für stabile Releases. Nutzer können `ref: v0.1.0` oder `ref: main` in ESPHome YAML verwenden.
4. **CI Pipeline:** ✅ GitHub Actions Workflow (.github/workflows/release.yml) zippt `custom_components/voice_alarm/` in `voice_alarm.zip` und veröffentlicht es als Release-Asset.

---

## References

- [ESPHome External Components](https://esphome.io/components/external_components/)
- [ESPBlog: Custom Components Removed](https://developers.esphome.io/blog/2025/02/19/about-the-removal-of-support-for-custom-components/)
- [HACS Custom Repositories](https://hacs.xyz/docs/comparing/custom)
- [Home Assistant Custom Component Docs](https://developers.home-assistant.io/docs/create_first_integration)

## Follow-up

- [#1](https://github.com/ChristianKuehnel/esp-home-voice-alarm/issues/1) — **User Installation Guide schreiben** (docs/installation.md mit Beispiel-YAML, GPIO-Mappings, Troubleshooting)