# ADR-006: Voice Commands (Stop & Confirmation)

**Status:** Final
**Created:** 2026-08-28
**Components:** ESPHome, Home Assistant
**Related PRDs:** FR-13 (Voice Stop), FR-14 (Verbal Confirmation)

---

## Context & Requirements

- **FR-13 (Voice Stop):** Der User soll einen laufenden Alarm stoppen können, indem er das Weckwort sagt und "Stop" (oder "Stopp") sagt.
- **FR-14 (Verbal Confirmation):** Der User soll eine Sprachbestätigung erhalten, wenn ein Wecker gesetzt oder gelöscht wurde.
- **Interrupt-Verhalten:** Wenn der Wecker klingelt und der User das Weckwort nutzt, soll der Alarm gestoppt UND der User verbal angesprochen werden.
- **Sprache & TTS:** Die Sprache und TTS-Pipeline werden analog zur globalen `voice_assistant`-Konfiguration (ESPHome YAML) gehandhabt. Keine separaten Sprachprofile pro Feature.
- **Pipeline:** ESPHome `voice_assistant` → Home Assistant Assist Pipeline (Intent-Erkennung, TTS).

---

## Decision: Alarm Stop Mechanism

**Entscheidung:** **Option 1 (ESPHome `voice_assistant` Standard-Intent) für v1, Option 2 für v2.**

### Option 1: Standard-Intent (Empfohlen für v1)
ESPHome `voice_assistant` hat eingebaute Standard-Intents (z.B. "Stop", "Cancel"). Diese können direkt vom ESPHome-Gerät erkannt werden.
- **Vorteile:** Offline-fähig (Intent-Erkennung lokal auf ESP32), keine Custom-Integration nötig, schnelle Implementierung.
- **Nachteile:** Begrenzte Flexibilität bei der Intent-Erkennung, potenzielle Kollision mit anderen Intents (z.B. "Stop Music").
- **Implementierung:** Der ESP32 erkennt "Stopp" als Standard-Intent und sendet eine MQTT- oder HA-Service-Message, um den Alarm zu löschen.

### Option 2: Custom Intent (Über HA Custom Component für v2)
Zukünftig kann eine Custom HA Integration verwendet werden, die spezifische Alarm-Intents (z.B. `StopAlarm`) behandelt.
- **Vorteile:** Volle Kontrolle, keine Kollisionen, komplexe Logik möglich.
- **Nachteile:** Benötigt HA-Verbindung (nicht offline), höhere Entwicklungskomplexität.

---

## Decision: Language & Confirmation

**Entscheidung:** **Sprache und TTS analog zur ESPHome `voice_assistant`-Konfiguration.**

### Sprache
Die Sprache der Voice Commands ist an die `language`-Konfiguration der `voice_assistant`-Komponente gebunden (z.B. `language: "de"` in der ESPHome YAML). Der User spricht Deutsch (oder die konfigurierte Sprache), und das System antwortet in derselben Sprache.

### Bestätigung (FR-14)
Statt eines einfachen Pieptons ("Fogbeep") verwendet das System volle Sprachbestätigungen über die HA TTS-Pipeline:
- **Wecker gesetzt:** "Okay, Wecker um 7 Uhr."
- **Wecker gelöscht:** "Okay, Wecker gelöscht."
- **Erinnerung gesetzt:** "Okay, Erinnerung in 20 Minuten."

Die TTS-Generierung erfolgt via HA (oder lokal via Piper, falls konfiguriert), die Antwort wird über die ESPHome `voice_assistant`-Pipeline zum User zurückgesprochen.

---

## Decision: Voice Assistant Interrupt Behavior

**Entscheidung:** **Option A (Alarm stoppen + Sprechen).**

### Verhalten
Wenn der Wecker klingelt (State: `FIRING`) und der User das Weckwort nutzt, um zu sprechen:
1. Der `voice_assistant` erkennt die Eingabe.
2. Der ESP32 stoppt sofort den Alarm (State: `IDLE`).
3. Der `voice_assistant` spielt die Bestätigung ab ("Okay, Alarm gestoppt. Wie kann ich helfen?").

### Begründung
- Der User erwartet eine Rückmeldung, dass der Alarm erfolgreich gestoppt wurde.
- Dies bietet eine konsistente User Experience: der Voice Assistant ist immer "hörbar" und reagiert auf Interaktionen.

---

## Implementation Plan (v1)

1.  **ESPHome YAML:**
    ```yaml
    voice_assistant:
      id: va_alarm
      microphone: microphone_1
      use_listen_indicator: true
      on_session_start:
        - logger.log: "Voice session started"
      # Standard-Intent 'stop' wird automatisch unterstützt
      # Wir mappen 'stop' auf die Alarm-Logik
      on_intent:
        - intent: Stop
          then:
            - alarm.stop
    ```
2.  **Alarm Stop Logic:**
    - Wenn `alarm.stop` getriggert wird, setze den internen State auf `IDLE`.
    - Stoppe den TTS-Ausgang (falls noch Musik läuft).
    - Sende Status-Update an HA.
3.  **TTS Confirmation:**
    - Nach erfolgreichem Stoppen: TTS via `voice_assistant` starten ("Okay, Alarm gestoppt.").