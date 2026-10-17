# REQUIREMENTS — captured product notes

*The note is the request; this file is the contract. It records what was asked
for on **2026-10-16 at 14:44** (two pages) without pretending it is built.
[`MEMORY.md`](MEMORY.md) holds the decisions, [`PLAN.md`](PLAN.md) the phases,
and [`AGENTS.md`](AGENTS.md) the rules that follow from them.*

---

## 1. Source note (verbatim)

> Dictated in Spanish. Spelling, punctuation, the inconsistent numbers, and the
> cut-off ending are preserved as received; they are evidence, not polished
> prose. The source marks the pages "Pág 1/2" and "Pág 2/2".

```text
Siendo el día de hoy 2026-10-16 y la hora 14:44. Hoy presentaré algunos detalles de la aplicación. Primero hay que crear el port del código de Rust creado para generar/obtener los días que son feriado en Perú.

Ahora creo que es súper importante tener todo lo necesario para poder crear aplicaciones. Estoy dándome cuenta de que lo más importante es poder generarlo rápidamente y sin ningún problema. Dado que yo usaré mucho más eso en el móvil es mejor, primero, crear para eso.

Dado que Android es ahora un sistema cerrado y complicado de lidiar con FOSS, entonces la idea es estar completamente centrado en la versión de escritorio, luego pensaré en Android.

Quiero además una aplicación de notas que me permita hacer una referencia perfecta a mi calendario.

Me encantaría que se pueda sincronizar con mi reloj Casio G-SHOCK, esto me permitiría estar alarmado inmediatamente cuando deba de dormir y cuando deba de levantarme.

Yo deseo dormir 10 horas siempre, por eso debe de haber una opción que automáticamente genere un horario de sueño, este "evento" de dormir es especial porque es un evento dinámico. Se puede ocultar y se le puede hacer aparecer y desaparecer automáticamente. Por esto yo planteé la opción de usar SQL, no estoy seguro de que la forma de almacenar el calendario sea una eficiente, por el lado de la encriptación creo que no es importante, ahora lo más importante es que la aplicación sea eficiente y sea la más rápida del mundo. Propongo trabajar con JSON, hay muchas librerías, una puede leer y escribir y otra solo leer con la ventaja que sea mucho más rápido. Aunque yo no estoy seguro si es que el formato de JSON es el mejor para este tipo de datos.

En la siguiente página trabajaré todo sobre el análisis de los datos que se van a utilizar.

**NOTA 2:** Para que trabajar en UTC es mejor. *(Pág 1/2)*

**NOTA:** Trabajar con otras zonas horarias y que aparezca con UTC. 
```

```text
Realizamos un reconocimiento del área de trabajo, se debe tener en consideración la existencia de varios modelos de calendario: calendario chino, calendario griego, indú. Lo más importante de todo esto es la idea que en todo el mundo un día comienza a las 0 horas, 0 minutos, 0 segundos, 0 microsegundos y termina a las 23 horas, 59 minutos, 59 segundos y 59 microsegundos. Puede ser falso, pero es la base más importante sobre la que vamos a basarnos. Se ha obtenido que un día con este sistema termina en un total de <u>86 399 segundos</u> con 59 microsegundos, una unidad más y cambia de día, pasa a ser el próximo día.

Se define el rango de horas disponibles de un día porque un día va exactamente desde 00:00 hasta antes del otro 00:00 considerando que un día tiene entonces 86 399 segundos, añadiendo un segundo más hace que el contador se reinicie. De el 80 000 segundo hasta 81 000 segundo hay 1 000 segundos, o sea desde 22:13:20 hasta 22:30 hay 00:16:40.

Prefiero realizar conteos con segundos, **día (s) = [0; 8700⟩** *(en recuadro)*.

Para sincronizar el tiempo entonces se usará un servidor NTP, la idea del cálculo de tiempo en local es únicamente con la finalidad de poder *(el texto queda cortado aquí)*
```

---

## 2. Normalized requirements

| ID | Requirement | State |
|---|---|---|
| **R1** | Peru holidays are generated/retrieved by the C++17 port of the original Rust code. | **done** — `libs/firstparty/pe-holiday-calc`; core wraps it in `klend_ario/holidays.hh` |
| **R2** | Build the **desktop version first**. Android is deferred until the desktop product is complete. | **locked** |
| **R3** | A **notes companion** can reference calendar events perfectly (by identity, not copied text). | **planned** — post-MVP |
| **R4** | Sync sleep/wake alarms to a **Casio G-SHOCK**. | **research** — optional |
| **R5** | Sleep is **10 hours by default**, generated automatically as a dynamic `sleep` event that can hide/show automatically. | **planned** — post-MVP |
| **R6** | **Performance is the top priority**: the app must be fast. Encryption is implemented but is not the current gate. | **locked** |
| **R7** | Storage format: **SQLite now**; **JSON is a hypothesis to benchmark**, not an adopted format. | **open** — benchmark first |
| **R8** | Work in **UTC**; support other time zones while keeping UTC visible. | **locked** |
| **R9** | Synchronize the wall clock with an **NTP server**; local time math is presentation only. | **planned** — runtime |
| **R10** | A day is a **half-open** interval of seconds: `[0, 86400)`. | **locked** — already how the code counts |

### R1 — the holiday port

The first note is already satisfied. `pe-holiday-calc` is the C++17 port of the
Rust generator; it owns the date math and the Peru rule table, and Klend-ario
consumes it only through `klend_ario/holidays.hh`. Do not start a second port or
copy holiday data into `core/`.

### R2 — desktop first

Linux desktop is the active target. **Phase 7 (Android) is deferred, not
cancelled.** No JNI, NDK, gradle, or Android SDK work until that phase is
explicitly reopened. The existing `android/` placeholder stays a placeholder.

### R3 — notes

A note must point at the exact calendar event, so a future `notes` table links
to `events.id` (a foreign key) instead of copying title/time text. The notes
surface is a post-MVP companion; it is not a new `EventType` and not a free-text
field that silently diverges from the event.

### R4 — G-SHOCK

The watch integration is a **research spike**, not a dependency. Feasibility
depends on the exact Casio model and the BLE/GATT protocol it exposes. Do not
promise a sync path, and do not add a Bluetooth dependency, before that spike
records what the device actually allows.

### R5 — the 10-hour sleep window

The default sleep duration is **10 hours**. The window is generated
automatically and is a **dynamic** event: it can be hidden and shown again
automatically by the scheduler. It remains user-overridable, and it is still a
normal row of type `sleep` so the weekly view, alarms, and MCP see one model.

### R6 + R7 — performance, SQLite, JSON, encryption

Performance is the requirement. That does **not** mean a speculative rewrite.
SQLite is the measured incumbent and stays until a JSON store beats it on the
operations that matter: open, range query, write, and Syncthing conflict
behaviour. JSON is attractive (a read/write library plus a faster read-only
one), but it is a hypothesis until benchmarked.

Encryption (SQLite3MultipleCiphers, ChaCha20-Poly1305) is already implemented
and stays. The note says encryption is not important *now*; removing it would be
a separate security decision, and its cost should be measured before it is
blamed for slowness.

### R8 + R9 — UTC, time zones, NTP

Unix seconds **UTC** is the canonical stored value, and the existing ISO-8601
parser/formatter is UTC-only on purpose. A local zone is a **display concern**:
other zones may be shown, but UTC remains available alongside them. NTP is a
runtime frontend concern; `core/` never opens a socket, and tests never touch a
live NTP server.

### R10 — the day model

The note says a day "ends at 86,399 seconds with 59 microseconds" and then
writes `día (s) = [0; 8700⟩`. Those are two different ideas:

- **86,399 s + 59 µs** is the *inclusive last instant* of a civil day.
- The *length* of the day is **86,400 whole seconds**, and the next midnight is
  the first instant of the next day.

The code uses the half-open interval `[0, 86400)` (see `core/src/event.cc`),
which is the only form that neither drops nor duplicates a second. The
`[0; 8700⟩` bound is inconsistent with both numbers; it is recorded as an open
question below, not adopted.

The note's worked example is a real test vector and is pinned by `event_test`:

| Instant | Seconds after 00:00 UTC |
|---|---|
| 22:13:20 | 80,000 |
| 22:30:00 | 81,000 |
| difference | 1,000 s = 16 min 40 s |

---

## 3. Open questions

1. **`[0; 8700⟩`** — is this a handwriting/transcription error for `86,400`, or a
   deliberately shorter counting range? Until confirmed, the code uses 86,400.
2. **Microseconds** — the model stores whole Unix seconds. If sub-second events
   are needed, add an explicit column/migration; do not fake it in a string.
3. **JSON vs SQLite** — defined by a benchmark, not by preference. The benchmark
   must cover the real weekly-range query, insert/update, open time, file size,
   memory, and a Syncthing conflict copy.
4. **Encryption cost** — measure before changing it. If it is a bottleneck, the
   decision is "make it faster", not "silently drop it".
5. **Notes schema** — one `notes` table with `event_id`, or a note that may span
   several events? The reference must be by id either way.
6. **G-SHOCK protocol** — which model, which BLE service, and whether alarms can
   be written at all. Research first; no dependency yet.
7. **Time-zone source and display** — system zone vs an explicit user setting,
   and how UTC is shown alongside local time. DST follows from that choice.

---

## 4. Traceability

| Where | What it carries |
|---|---|
| [`MEMORY.md`](MEMORY.md) §1, §3, §4, §5, §6, §10 | The decisions and the reasoning |
| [`AGENTS.md`](AGENTS.md) §2, §4, §7 | The invariants an agent must not break |
| [`PLAN.md`](PLAN.md) | Phase 7 deferral and phases 8–10 |
| [`TESTING.md`](TESTING.md) | The day/time vectors and the no-network rule |
| `core/include/klend_ario/event.hh` | The UTC/half-open-day comment |
| `core/tests/event_test.cc` | The 80,000 / 81,000 / 1,000 test |
| [`../manifest.json`](../manifest.json) | Desktop-only active platform and the storage/time metadata |
