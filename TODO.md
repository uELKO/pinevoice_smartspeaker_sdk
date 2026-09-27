# PineVoice SDK — TODO

## OTA-Firmware-Update (über MQTT/HA getriggert)

Noch nicht begonnen. Grobschätzung: 3-5 Tage, größter Unsicherheitsfaktor ist die
Hardware-Validierung (nicht die Codemenge).

Vorhanden, aber nicht verdrahtet:
- Partitionstabelle hat bereits echte A/B-Slots für "FW"
  (`boards/bl606p_pinevoice_e907/configs/partition.toml`: address0=0x10000,
  address1=0x77C000, je 0x76C000)
- `components/bootab` — A/B-Boot-/Slot-Switching-Logik
- `components/fota` — FOTA-Client (URL-Download, Verify, Events)

Fehlt:
- `app_fota_start()` hat keine Implementierung; Aufruf in
  `solutions/pinevoice_fw_e907/app/src/wifi/app_net.c` steht hinter
  `#if defined(APP_FOTA_EN) && APP_FOTA_EN`, das nirgends definiert ist (toter Code)
- MQTT-Trigger für Update-URL (analog zum bestehenden Restart-Befehl der
  HA-Device-Card)
- `package.sh` erzeugt kein `.ota`-Update-Image, nur ein Flat-Zip für Vollflash

Offene Fragen vor Umsetzung:
- Boot2 (`bootimgs/boot2_isp_release.bin`, vorgefertigte Binärdatei, nicht aus
  diesem Repo gebaut) — schaltet sie den A/B-Slot über `bootab` wirklich sauber
  um? Nie in diesem Fork getestet. Risiko: unbootbares Gerät bei Fehlschlag,
  Recovery nur über UART.
- FW-Partition hat `security = 1` gesetzt — vermutlich Secure-Boot/Signatur-Pflicht.
  Muss geklärt werden, wie aktuell signiert wird, sonst schlägt
  `fota_verify.c` beim neu heruntergeladenen Image fehl.

Sinnvoller Ansatz: MQTT-Befehl mit Firmware-URL → `fota_open()`/`fota_download()`
→ verifizieren → `bootab` schaltet Slot um → Reboot. Optional später: HA-natives
`update`-Entity via MQTT Discovery statt Custom-Trigger.

## Barge-in: Wake-Word während TTS-Wiedergabe / paralleles Zuhören während Musik

Noch nicht begonnen. Live getestet (2026-08-14): Wake-Word während laufender
Sprachausgabe wird "extrem schwer verstanden" — dasselbe Problem wie beim
parallelen Musikhören (siehe Konversation).

Ursache (Code-Analyse):
- Software blockiert eine erneute Wake-Detection während TTS-Playback nicht
  (`is_streaming`/`is_paused` in `satellite_mode_wake_stream.c` betreffen nur
  die STT-Aufnahmephase, nicht die Antwort-Wiedergabe) — ein Barge-in wäre also
  grundsätzlich architektonisch möglich.
- Akustisch fehlt aber eine echte AEC-Kopplung an die laufende Wiedergabe. Der
  einzige vorhandene Mechanismus (`MIC_CTRL_NOTIFY_PLAYER_STATUS` in
  `app_event.c`, `EVENT_STATUS_SESSION_START`) deckt nur das kurze Wake-Chime
  (`wakeup.wav`, ~200ms) ab, nicht die eigentliche, oft mehrsekündige
  TTS-Antwort. Das Mikro hört die eigene Lautsprecherausgabe dabei ungefiltert
  mit.

Es gibt einen AEC-Pfad im Vendor-Stack (`voice laec 0|1` CLI-Kommando in
`cli_voice.c`, ruft `aui_mic_control(MIC_CTRL_ENABLE_LINEAR_AEC_DATA, ...)`),
aber:
- **Live-Test von `voice laec 1` hat die USB-Konsole zum Hängen gebracht**
  (Gerät selbst blieb funktionsfähig — Wake-Word ging weiter —, aber keine
  Konsolenausgabe mehr, auch nach Neuverbindung). Deutet auf unvollständige/
  instabile Verdrahtung dieses Features im Vendor-SDK hin. Vor weiterer Nutzung
  browsen, was `MIC_CTRL_ENABLE_LINEAR_AEC_DATA` auf der C906-Seite tatsächlich
  tut (`components/bl606p_service/cxvision`, `voice_mind.cc`
  `ENABLE_LINEAR_AEC_DATA_CMD`).
- Unklar, ob dieser AEC-Pfad überhaupt die richtige Referenz bekommt (das
  wiedergegebene Signal muss dem AEC als Referenz zugeführt werden, nicht nur
  ein An/Aus-Flag) — reine Vermutung, nicht verifiziert.

Ein separates, kleineres Feature (button-getriggerter Abbruch von
Zuhören/Sprechen zurück in idle) wurde besprochen, aber zurückgestellt — siehe
Konversation vom 2026-08-14 für den Entwurf (neue `wsat_audio_stop_send()` in
der Satellite-Lib, lokales Verwerfen einer danach eintreffenden TTS-Antwort).

## Follow-up-Fragen: automatisch weiterhören ohne erneutes Wake-Word (Wyoming) — VERWORFEN

Ursprünglicher Wunsch: wenn der Assistent eine Rückfrage stellt, soll PineVoice
direkt nach Ende der Sprachausgabe wieder zuhören (wie Alexas Follow-up-Modus).

**Verifiziert (2026-09-26) im HA-Core-Quellcode:** Das geht mit Wyoming aktuell
gar nicht, unabhängig von unserer Firmware. `homeassistant/components/wyoming/
assist_satellite.py` hat keinen `INTENT_END`-Handler — `continue_conversation`
wird nie an Wyoming-Satelliten weitergereicht. Von den drei
Assist-Satellite-Diensten (`announce`, `ask_question`, `start_conversation`)
implementiert Wyoming nur `announce`; die beiden anderen würden sofort
`NotImplementedError` werfen. Automatisierungen können das auch nicht
umgehen: `assist_pipeline` feuert nirgends ein Event auf den HA-Event-Bus,
`continue_conversation` existiert nur intern in der Pipeline.
`homeassistant/components/esphome/assist_satellite.py` hat diese Anbindung
dagegen vollständig. Das unbehandelte `"transcribe"`-Event aus unserem
Mitschnitt war eine falsche Spur — das schickt HA bei jedem STT-Start, nicht
speziell für Follow-ups.

→ Das ist der Hauptauslöser für die ESPHome-Migration unten. Wird durch die
gelöst, nicht separat weiterverfolgt.

## ESPHome-Native-API statt Wyoming (großer Umbau) — Phase 1 läuft

**Status (2026-09-26): Phase 1 (Verbindungs-Lebenszyklus, kein Voice Assistant)
ist implementiert (`components/esphome_native_api`, Plaintext-Framing, kein
Noise) und aktiv (`wyoming_init()` in
`solutions/pinevoice_fw_e907/app/src/wyoming/wyoming.c` startet den
ESPHome-API-Server jetzt bei jedem Boot).**

**Hänger-Bug behoben, per Live-Konsole bestätigt:** Der beim ersten Test
aufgetretene komplette Hänger (kein Wake-Word, keine Tasten außer Mute,
LED-Ring auf Rot-Fehleranimation, deterministisch über Hard-Powercycle
reproduzierbar) trat nach den beiden folgenden Fixes nicht mehr auf, über
einen sauberen Powercycle mit von Anfang an mitlaufender Konsole beobachtet:
- `MDNS_MAX_SERVICES` war in diesem lwIP-Build auf **1** begrenzt; Wyoming
  belegte den einzigen Slot bereits, `esphome_mdns_advertise_start()`s
  `mdns_resp_add_service()`-Aufruf für `_esphomelib._tcp` konnte also nie
  erfolgreich sein. Auf **2** erhöht (`boards/bl606p_pinevoice_e907/include/
  lwipopts.h`).
- `esphome_server_task`s Retry-Schleife bei `accept()`-Fehlern hatte kein
  Backoff (`aos_msleep(200)` ergänzt in `components/esphome_native_api/src/
  esphome_server.c`).

Boot-Log zeigt sauberen Start: `esphome_api esphome_server.c[176]:
listening on port 6053` direkt nach `wyoming.c[333]: Wyoming init`, danach
normaler WiFi/DHCP/MQTT-Verlauf, keine Auffälligkeiten. Welcher der beiden
Fixes ursächlich war (oder beide), ist nicht einzeln isoliert — aber der
Hänger ist weg, nicht nur eine Vermutung.

**mDNS-Discovery und HA-Verbindung bestätigt (2026-09-26):** HA hat
`pinevoice-2a893b` über `_esphomelib._tcp.local.` gefunden und als
ESPHome-Integration angeboten; nach "Hinzufügen" per Live-Konsole
verifiziert. Dabei einen zweiten Lücke gefunden und behoben: `ConnectRequest`
(msg_type 3) wurde trotz `uses_password=false` von `aioesphomeapi` gesendet
und war unbehandelt ("unhandled msg_type 3") — gehört genauso zum
Verbindungs-Lebenszyklus wie Hello/DeviceInfo, war in Phase 1 aber
übersehen worden. `ConnectResponse` jetzt implementiert
(`components/esphome_native_api`), erneut gegen echtes HA 2026.9.2
verifiziert: keine unbehandelten `ConnectRequest`s mehr, LED/Wake-Word
weiterhin normal.

**Noch offen:** Es wurden noch keine echten Entities gemeldet
(`ListEntitiesRequest` beantworten wir bewusst leer, s.o.), und mehrere von
HA gesendete Requests werden weiterhin bewusst ignoriert (`SubscribeStates`,
`SubscribeHomeassistantServices`, `SubscribeHomeAssistantStates`,
`SubscribeVoiceAssistantRequest`, sowie ein bisher nicht identifizierter
Typ 121 — vermutlich eine Voice-Assistant-Konfigurationsabfrage, ausgelöst
durch die in `DeviceInfoResponse` gesetzten Voice-Assistant-Feature-Flags).
Alle bisher beobachtet ohne negative Auswirkung (Verbindung bleibt stabil),
aber nicht einzeln verifiziert, was HA bei fehlender Antwort tatsächlich
erwartet. Nächster sinnvoller Schritt: Phase 2 (Voice Assistant Ablauf), die
`SubscribeVoiceAssistantRequest` ohnehin echt beantworten muss.

Ansonsten Grobschätzung weiterhin **2-4 Wochen**, deutlich größer als alles
bisher Gemachte. Motivation: Wyoming implementiert bei Home Assistant kein
automatisches Weiterhören nach Rückfragen (siehe oben) und wirkt insgesamt wie
der weniger aktiv gepflegte Pfad — das Referenzprojekt `rhasspy/wyoming-satellite`
ist archiviert, zugunsten eines ESPHome-basierten Nachfolgers. Nabu Casas
eigene Hardware ("Home Assistant Voice PE") spricht ESPHomes natives API.

Bestätigt per Recherche im ESPHome-`api.proto` und `aioesphomeapi`-Client:
`VoiceAssistantRequest` wird vom **Gerät** an HA geschickt (`start`,
`conversation_id`, Flags wie `USE_WAKE_WORD`/`USE_VAD`); bei `INTENT_END`
schickt HA `continue_conversation` als Datenfeld zurück
(`VoiceAssistantEventData{name:"continue_conversation", value:"1"}`). Das
Gerät müsste bei `continue_conversation=1` nach Ende der Wiedergabe einfach
selbst ein neues `VoiceAssistantRequest` mit `USE_VAD`-Flag (ohne Wake-Word)
und derselben `conversation_id` schicken — nativ vom Protokoll unterstützt,
nicht nachgerüstet.

**Ersetzt komplett**: `components/wyoming_c_satellite` (die ganze
Protokoll-Bibliothek: TCP-Server, JSON-Event-Framing, Mic/Snd-Komponenten-
Interface) sowie die App-Glue in `app/src/wyoming/wyoming.c`.

Neu zu bauen:
1. ~~**Transport**: TCP-Server + mDNS~~ — **gebaut** (Phase 1, s.o.), aber
   mDNS-Teil wegen `MDNS_MAX_SERVICES==1` vermutlich wirkungslos, s.o.
2. **Verschlüsselung (größtes Risiko, noch nicht angefangen)**: ESPHome
   erwartet standardmäßig Noise-Protocol-Verschlüsselung
   (`Noise_NNpsk0_25519_ChaChaPoly_SHA256`). Kryptobausteine (X25519,
   ChaCha20-Poly1305, SHA256, HKDF) sind über mbedTLS schon vorhanden (nutzen
   wir schon für TLS/MQTT), aber die Noise-Handshake-Logik selbst muss neu
   geschrieben werden. Keine fertige, zu unserem SDK passende C-Bibliothek
   dafür (`rweather/noise-c` existiert, wäre aber eine neue
   Fremdabhängigkeit). Bugs hier sind schwer zu debuggen (kryptografisch,
   nicht im Klartext sichtbar wie Wyomings JSON). Plaintext-Verbindungen
   werden von `aioesphomeapi` weiterhin voll unterstützt (verifiziert,
   `_frame_helper/plain_text.py` ist kein Legacy-Pfad) — Sequenzierung
   bewusst "erst alles unverschlüsselt zum Laufen bringen, Noise zuletzt".
   Phase 1 nutzt bereits Plaintext, kein Custom-Protobuf-Codec nötig — hand-
   geschriebener Wire-Format-Code in `components/esphome_native_api`
   (`pb_wire.c`) reicht für den bisherigen kleinen Nachrichtensatz.
3. ~~**Protobuf-Codec**~~ — s.o., handgeschrieben statt nanopb, bisher
   ausreichend für den kleinen Nachrichtensatz aus Phase 1.
4. ~~**Verbindungs-Lebenszyklus**: Hello/Connect-Handshake, DeviceInfo-Antwort,
   (meist leere) ListEntities-Antworten, Ping-Keepalive.~~ — **gebaut, gegen
   echtes HA 2026.9.2 getestet, funktioniert** (Phase 1, s.o.).
5. **Voice-Assistant-Ablauf** (begonnen 2026-09-26): `SubscribeVoiceAssistantRequest`
   (Subscribe-Flag + Flags merken) und `VoiceAssistantConfigurationRequest`/
   `-Response` (msg 121/122 — meldet den einen fest eingebauten On-Device-
   Wake-Word "alexa"/"Alexa" zurück) sind **gebaut und gegen echtes HA
   2026.9.2 verifiziert**: HA zeigt jetzt "Assist-Satellit: Leerlauf" (vorher
   "Nicht verfügbar") und "Aktivierungswort: Alexa" (vorher "unavailable") im
   ESPHome-Geräte-Dashboard. `VoiceAssistantSetConfiguration` (msg 123, zum
   Wechseln des aktiven Wake-Words) wird noch nicht behandelt — unkritisch,
   da es ohnehin nur das eine fest verdrahtete Wake-Word gibt, ein Wechsel
   wäre sowieso wirkungslos.

   Nebenbefund, nicht weiter verfolgt: `SubscribeVoiceAssistantRequest`s
   `flags`-Feld kam von echtem HA als `0x4`, obwohl `VoiceAssistantSubscribeFlag`
   in `api.proto` nur `API_AUDIO = 1` kennt. Ursache nicht geklärt (evtl.
   neuere/interne HA-Flags, die in der öffentlichen `main`-`api.proto` noch
   fehlen) — wir speichern den Wert nur, ohne aktuell darauf zu reagieren,
   also ohne Auswirkung.

   **Update (2026-09-26): VoiceAssistantRequest/Response-Roundtrip gebaut und
   gegen echtes HA verifiziert** — aber bewusst noch nicht an den echten
   Wake-Word-Callback angeschlossen, sondern nur über einen neuen CLI-
   Testbefehl `esphome_va_test` (`app/src/wyoming/wyoming.c`,
   `esphome_api_send_voice_assistant_start()` in
   `components/esphome_native_api`) manuell auslösbar — Risiko für die
   Alltagsnutzung damit praktisch null, da der bestehende Wyoming-Wake-Word-
   Pfad komplett unangetastet bleibt. Testergebnis: `VoiceAssistantRequest`
   gesendet → `VoiceAssistantResponse: port=0 error=0` kam nach ~13ms zurück
   → HA startete sofort einen echten Pipeline-Run und schickte drei
   `VoiceAssistantEventResponse` (msg 92) hinterher (aktuell nur geloggt,
   noch nicht geparst). Damit verifiziert: **`port=0` bedeutet tatsächlich
   "Audio über die API-Verbindung, kein separates UDP"** (vorher nur
   Vermutung). Alle Feldnummern für VoiceAssistantRequest/Response/
   EventResponse/Audio wurden zusätzlich direkt gegen das echte `api.proto`
   von `esphome/aioesphomeapi` (main-Branch) gegengeprüft und stimmten exakt
   — keine Korrekturen nötig (im Gegensatz zu den zwei Lücken weiter oben,
   die dabei gefunden wurden: `ConnectRequest`/-`Response` heißen inzwischen
   `AuthenticationRequest`/-`Response`, gleiche IDs 3/4, als deprecated
   markiert aber weiterhin vom echten Client gesendet).

   **Update (2026-09-26, Fortsetzung): kompletter Pipeline-Roundtrip per CLI-
   Testbefehl verifiziert — STT → Intent → TTS funktioniert bereits
   End-to-End über die ESPHome-API gegen echtes HA.** Ablauf: `esphome_va_test`
   sendet `VoiceAssistantRequest` mit Flags = **nur `USE_VAD`** (siehe
   wichtige Korrektur unten), streamt danach 5s Mic-Audio als
   `VoiceAssistantAudio` (Tap in `mic_streamer_fn`, zusätzlich zum
   unveränderten `wsat_mic_write_data()`-Aufruf, per `s_va_test_streaming`-
   Flag). Ergebnis: `VoiceAssistantEventResponse` (jetzt geparst,
   `handle_voice_assistant_event()`) zeigte den vollen Ablauf RUN_START →
   STT_START → STT_VAD_START/END → **STT_END mit echtem Transkript** →
   INTENT_START/END mit echter Konversationsantwort → TTS_START/STREAM_START/
   END mit TTS-URL → RUN_END.

   **Wichtige Korrektur:** `USE_WAKE_WORD`-Flag beim ersten Testlauf gesetzt
   → sofortiger Fehler `wake-engine-missing` / "No wake word engine". Das
   Flag heißt zwar "use wake word", bedeutet aber "HA soll selbst eine
   Wake-Word-Erkennungsstufe ausführen" — nicht "Wake-Word wurde schon
   erkannt". Für ein Gerät mit reiner On-Device-Wake-Word-Erkennung (wie
   PineVoice) ist das falsch; richtig ist **nur `USE_VAD`**, damit die
   Pipeline direkt bei STT startet. Wichtig für die spätere echte Verdrahtung
   an den Wake-Word-Callback.

   **Zweiter gefundener und behobener Bug:** Nach dem ersten erfolgreichen
   Roundtrip riss die Verbindung mit `malformed/oversized frame, closing
   connection` ab (harmloser Sofort-Reconnect, aber ein echter Bug).
   Ursache: `ESPB_RX_BUF_SIZE` war 512 Bytes, HA schickt aber TTS-Audio
   tatsächlich per `VoiceAssistantAudio` (msg 106) über dieselbe
   API-Verbindung (nicht nur die URL in den Event-Daten) — beobachteter
   Chunk war 1027 Bytes. Auf 4096 erhöht (`rx_buf` ist `static`, kostet also
   BSS statt Task-Stack), danach kein Abriss mehr, derselbe Chunk kam sauber
   an (aktuell nur geloggt, noch nicht abgespielt). Damit auch geklärt: TTS
   kommt **sowohl** als URL in den Event-Daten **als auch** als Rohaudio über
   `VoiceAssistantAudio` — device kann sich aussuchen, welchen Weg es nutzt;
   da die Bytes ohnehin ankommen, spricht das für "einfach die gepushten
   Bytes abspielen" statt zusätzlich per HTTP zu holen.

   All das bisher **ausschließlich über den CLI-Testbefehl**, komplett
   unabhängig vom echten Wake-Word-Pfad — Risiko für die Alltagsnutzung war
   die ganze Zeit praktisch null.

   **Update (2026-09-26, 3. Fortsetzung): TTS wird tatsächlich hörbar
   abgespielt, Audio-Stream stoppt jetzt event-gesteuert statt per Timer.**
   `esphome_api_set_voice_assistant_event_callback()` (neue generische
   Callback-API in `components/esphome_native_api`) liefert jedes
   `VoiceAssistantEventResponse` als `(event_type, name, value)` an die App.
   `wyoming.c`s `va_test_event_cb()` nutzt das für zwei Dinge: (1) stoppt das
   Audio-Streaming bei `STT_END`/`ERROR`/`RUN_END` (mit 15s-Timeout als reines
   Sicherheitsnetz, falls keins davon je kommt) statt nach einer festen
   Dauer; (2) spielt bei `TTS_END` die mitgelieferte URL über einen
   eigenständigen `player_t` direkt ab (`player_play(player, "http://...",
   0)` — funktioniert mit beliebigen HTTP-URLs, wie schon vom
   `smta play <url>`-CLI-Befehl bekannt). **Live gegen echtes HA getestet
   und akustisch bestätigt**: STT transkribierte echte Sprache, HA antwortete
   konversationell, die Antwort kam hörbar aus dem Lautsprecher — bei
   unverändert normalem LED/Wake-Word/Tasten-Verhalten.

   Nebenbefund: HA schickt die TTS-Audiodaten *zusätzlich* ungefragt als
   `VoiceAssistantAudio`-Chunks (msg 106, ca. 60× je 1027 Bytes plus ein
   kleinerer Schluss-Chunk) über dieselbe Verbindung, obwohl wir den
   URL-Weg nutzen — aktuell einfach verworfen (unhandled), verschwendet aber
   etwas Bandbreite. Nicht dringend: entweder künftig den Push-Weg nutzen
   (spart die zusätzliche HTTP-Anfrage) oder das Verhalten so lassen.

   Eigenständige Test-Player-Instanz (`s_va_test_player`, komplett getrennt
   von Wyomings `g_player`) bewusst gewählt, um Wyomings bestehende,
   funktionierende Wiedergabe-Pipeline nicht anzufassen.

   **Update (2026-09-27): echtes Wake-Word löst jetzt den ESPHome-Pfad aus —
   live getestet, funktioniert.** `mic_evt_cb`s `MIC_EVENT_SESSION_START`
   verzweigt jetzt auf `s_esphome_wake_enabled` (Default `false`): entweder
   der unveränderte `wsat_wake_detection()`-Pfad, oder `esphome_wake_trigger()`
   — signalisiert nur einen Semaphore an einen eigenen, dauerhaft laufenden
   `esphome_wake`-Task (`esphome_wake_worker_fn`), damit der Mic-Callback nie
   auf Netzwerk-I/O wartet. Beide Pfade rufen dieselbe, aus dem CLI-Testbefehl
   extrahierte `esphome_run_voice_assistant_session()` — exakt das gegen HA
   verifizierte Verhalten von oben, jetzt über zwei Trigger erreichbar.
   `s_esphome_wake_enabled` wird **bewusst nicht in KV persistiert** — jeder
   Reboot setzt zurück auf Wyoming, damit ein missglückter Live-Test die
   Alltagsnutzung nie dauerhaft strandet (Power-Cycle statt Reflash genügt).
   Umschaltbar per neuem CLI-Befehl `esphome_wake_enable 0|1`.

   Getestet: mit Schalter aus (Default) keinerlei Verhaltensänderung
   bestätigt (LED/Wake-Word/Tasten normal) — dieser Stand zuerst committet.
   Danach live mit Schalter an und echtem gesprochenem "Alexa" getestet:
   STT transkribierte "Wie spät ist es?" korrekt, HA antwortete "Es ist
   11:04", TTS wurde hörbar abgespielt — Stimme und Reaktionszeit normal.
   Einzige Auffälligkeit (laut Nutzer bereits vorher bei Wyoming vorhanden,
   also keine Regression): LED wird bei Wake-Word-Erkennung nicht heller.

   **Update (2026-09-27, Fortsetzung): LED-Show jetzt an die echten
   ESPHome-Events gekoppelt, live bestätigt.** `esphome_run_voice_assistant_
   session()`/`va_test_event_cb()`/`va_test_player_event()` setzen jetzt
   `LIGHT_SHOW_LISTENING` (Start des Turns), `LIGHT_SHOW_PROCESSING`
   (`STT_END`), `LIGHT_SHOW_ANSWER` (`TTS_START`), `LIGHT_SHOW_ERROR` (mit
   vorherigem Reset auf `READY`, wie bei Wyoming) bei jedem Fehlerpfad, und
   `LIGHT_SHOW_READY` erst wenn die TTS-Wiedergabe tatsächlich fertig ist
   (nicht bei `RUN_END`, das kommt schon an, bevor der HTTP-Fetch/die
   Wiedergabe abgeschlossen ist) — Eins-zu-eins-Analogie zu
   `fback_handle_sys_event()`s Wyoming-Mapping. Live mit echtem Wake-Word
   getestet: kompletter LED-Zyklus (zuhören → verarbeiten → antworten →
   bereit) bestätigt, funktioniert wie bei Wyoming.

   Damit ist der ESPHome-Pfad technisch fähig, den kompletten Wake-Word-
   Ablauf inklusive LED-Feedback zu übernehmen. Noch offen, bevor das der
   **dauerhafte** Standard werden sollte (aktuell weiterhin Wyoming per
   Default, ESPHome nur bewusst per CLI zugeschaltet):
   - Mehrfachauslösung/Robustheit bei schnell wiederholtem Wake-Word nicht
     geprüft (Semaphore-Queue verarbeitet sequenziell, aber ungetestet, wie
     sich das UX-mäßig anfühlt)
   - Persistenz-Entscheidung: aktuell bewusst flüchtig (siehe oben) — bevor
     ESPHome dauerhaft Standard wird, müsste diese Entscheidung nochmal
     bewusst getroffen werden (z. B. per KV, sobald genug Vertrauen besteht)
6. **Follow-up-Logik**: bei `INTENT_END` mit `continue_conversation=1` die
   `conversation_id` merken, nach Ende der Wiedergabe automatisch neues
   `VoiceAssistantRequest` mit `USE_VAD` (ohne Wake-Word) + derselben
   `conversation_id` schicken.
7. **Announce-Äquivalent**: `VoiceAssistantAnnounceRequest` behandeln
   (entspricht Wyomings audio-start/chunk/stop, das wir schon nutzen).

Bleibt unverändert wiederverwendbar:
- `microwakeword` (Wake-Word-Erkennung, C906) — protokollunabhängig
- Mic-Capture (`aui_mic_control`, `MIC_EVENT_SESSION_START`) und
  Sound-Playback (`player_t`/`nsfifo`-Pipeline) — nur die äußeren `wsat_*`-
  Aufrufe müssten getauscht werden
- **MQTT-HA-Device-Card (Restart/LED/Volume) bleibt vorerst unangetastet
  parallel bestehen** — bewusst nicht auf native ESPHome-Entities umgezogen,
  um die Baustelle klein zu halten (Entscheidung vom 2026-09-26)
- WiFi-Provisioning, USB-Konsole, gesamte `app_sys`/`app_net`-Infrastruktur

Wichtig: löst **nicht** das AEC/Barge-in-Problem oben — das ist ein reines
Audio-Hardware-Thema, unabhängig vom Satellite-Protokoll.

Offene Fragen vor Umsetzung:
- Exakter Ablauf von HA-seitigem `handle_pipeline_start` / wie genau
  `aioesphomeapi` die device-initiated `VoiceAssistantRequest` gegenkontert —
  grob verstanden, aber nicht bis ins letzte Detail nachvollzogen.
- Läuft eine unverschlüsselte (Legacy-Plaintext-Passwort) Verbindung mit
  aktuellen HA-Versionen überhaupt noch zuverlässig, um den Noise-Handshake
  erstmal zu vertagen? Müsste geprüft werden, bevor man sich für "Noise
  zuerst" oder "Plaintext zuerst, Noise später" entscheidet.
