# HANDOFF — OmniPad-PS5 integración (rama `integration`)
Repo: https://github.com/diegobarbosaa/OmniPad-PS5  |  Usuario: gabrielskaminski (su PR = #17)
Método: sprints cortos; actualizar este archivo al final de cada sprint.

## Reproducir el estado
```
git clone https://github.com/diegobarbosaa/OmniPad-PS5 && cd OmniPad-PS5
for n in 17 18 19 20 21; do git fetch origin pull/$n/head:pr-$n; done
git bundle: ver omnipad-integration.bundle (git clone omnipad-integration.bundle -b integration)
```

## PRs abiertos (5) — TODOS mergeados en `integration`
| PR | Autor | Qué hace | Estado |
|----|-------|----------|--------|
| #17 | gabrielskaminski | Xbox GIP + USB_FS IN reliability (fix #12) | MERGEADO limpio |
| #21 | jpemendonca | LED jugador XInput (8BitDo Ultimate 2C) | MERGEADO limpio |
| #18 | BearTS | Stack BT AnyPad, vibración DualSense, perfil Ultimate 2, dashboard | MERGEADO limpio |
| #19 | SacaSoh | GameSir G7 HID, retención por slot, atajo PS | MERGEADO con resolución manual |
| #20 | SacaSoh | Parser Nintendo full-report + init USB | MERGEADO con resolución manual (Sprint 2) |

## Decisiones de resolución
**#19** — ps5_vpad.c watchdog: lógica por-slot de #19 pero SOLO vigila `CONN_NETWORK_STREAM` o timeout > default (respeta #18: USB/BT event-driven). Se conservan `vpad_set_remap` (#18) y `vpad_update_level/_cached_level` (#19). usb_hotplug.c: rama Xbox GIP (#17) + sin fallback HID genérico para G7PRO/G7SE.
**#20 (Sprint 2)** — usb_controllers.c: se conservan `gamesir_parse_gamepad` (#19) y `usb_nintendo_switch_send_cmd` (#20). usb_hotplug.c: ACK Nintendo 0x81 se consume antes del parseo; rama GIP intacta; fallback HID genérico excluido para G7PRO, G7SE y NINTENDO_SWITCH_PRO. Makefile: se añaden ambos bloques de tests. docs/CONTRIBUTION_SCOPE.md: secciones separadas #19 / #20.
**Tick del worker USB** — `usleep(is_xbox_one ? 250 : 4000)` bajo `__PROSPERO__` (250 µs solo GIP/USB_FS de #17; 4 ms ≈250 Hz para el resto, #19). Fuera de Prospero, 4000. Resuelve la duda de CPU/latencia.

## Estado de tests (Sprint 2)
`make test` con `clang` → shim a gcc (`ln -s /usr/bin/gcc /tmp/bin/clang; PATH=/tmp/bin:$PATH make test`): **rc=0**. 6 suites SKIP (fuentes ausentes upstream: normalization, lifecycle_logout, fuzz_usb, hotplug_thrashing, web_concurrency, dongle_cable_switch) — no son "passed".
Arreglos de build: Makefile añade `src/generic.c src/crc32.c` a los links con profiles.c y `src/remap.c` a test_vpad_*.
**Tests modificados (REVISAR con los autores):**
- `test_parsers.c::test_machenike_g5_pro_wired_usb_c`: `profiles_parse_report()` ya no existe; ahora solo se asserta identificación (`CTRL_XBOX_XINPUT`), se perdió la aserción de parseo CROSS (el buffer es HID-style, no un frame XInput). Debilita el test: reescribir con un frame XInput real.
- `test_vpad_retention.c` y `test_vpad_ps_level.c`: ahora asertan que un pad USB con timeout por defecto NO se limpia por silencia (política #18), contradiciendo la intención original de #19 (500 ms). Decisión de política pendiente del mantenedor.
- No se probó nada en hardware ni compilación PS5 SDK (`__PROSPERO__` no se compila en host).

## Sprint 3 (sin hardware) — hecho
- Test Machenike reescrito con frame XInput real (0x00/0x14, A→CROSS, LT): vuelve a asertar parseo.
- **#10 Mad Catz SF IV FightPad (0738:8828)**: nuevo `CTRL_MADCATZ_SF4_FIGHTPAD` (añadido al final del enum), parser de 27 bytes según la captura del reportero, sin fallback HID genérico, `tests/test_madcatz_fightpad.c` en el Makefile. Sin validar en hardware; diagonales del hat asumidas (no capturadas); bytes de presión (7-10) ignorados.
- **#1 Pair Controller**: `web/index.html` tiene botón "Pair controller" (+ `/api/bt/pair`, `/api/bt/scan`) → resuelto por #18; cerrar tras verificar en consola.
- **#15 Rumble XInput**: ya hay rumble GIP y XInput (`0x00 0x08 0x00 s w`) en `usb_send_rumble` (solo `__PROSPERO__`); no verificable en host → mantener abierto hasta prueba con GameSir Cyclone 2.
- `ANYPAD_BUILD` 21→22 (la cadena de versión sigue 1.0.4: subirla es decisión de release). docs/COMPATIBILITY.md: filas G7 Pro/SE, Mad Catz, Switch.
- `make test` rc=0 (7 suites nuevas/viejas pasan, 6 SKIP por fuentes ausentes).

## Pendiente (en orden)
1. Compilar con PS5 SDK (bloques `__PROSPERO__`: hotplug, #20, tick 250/4000 µs) y probar en consola.
2. Decidir política de retención USB (#18 event-driven vs #19 500 ms); hoy gana #18 y los tests lo reflejan.
   - Evidencia de campo (comentario de jpemendonca en #19, FW 13.60, 8BitDo Ultimate 2C por dongle 2.4GHz 2dc8:3106): el dongle solo envía al cambiar; con el bucle 1.0.4 (timeout 15 ms, reinicio por llamada) se perdían toques y direcciones/gatillos sostenidos. Con un IN transfer armado sin timeout, re-armado tras USB_FS_COMPLETE, chequeo de complete.ep_index y sin auto-neutral de 500 ms en slots USB, dejó de perder entradas (DS4 054c:05c4 sigue OK). Respalda #17 y la política #18 (event-driven) sobre los 500 ms de #19. Ojo: probó su propia implementación, no esta rama.
   - Verificado en código (host): `usb_fs_recv_packet` en usb_hotplug.c es genérico (timeout 0, in_armed, re-arma tras COMPLETE, valida ep_index), aplica también a XInput/HID, no solo a GIP. El dongle 2C es XInput (EP 0x81).
3. Release: decidir versión (README/docs aún dicen 1.0.4 y "sin Bluetooth", desactualizado tras #18).
4. Dashboard: exponer `stale_timeout_ms` (opcional; web.c no choca con #19).
5. Subir rama/PRs: sin credenciales aquí; usar el bundle.
6. Bloqueados por datos: #7 Stadia y #8 8BitDo Adapter v2 (el reporte solo dice "reconoce Xbox Series pero no hace nada": pedir lsusb/descriptor y captura de reportes), #14 Flydigi Vader 4 Pro (touchpad), #2,#3,#4,#5,#6,#11,#13.

## Issues candidatos (abiertos #1-#16; #9 y #17-21 son PRs)
- #10 Mad Catz SF IV FightPad (VID 0738 / PID 8828): agregar VID/PID + perfil HID → FÁCIL
- #7 Stadia Controller, #8 8BitDo Wireless Adapter v2: añadir VID/PID/perfil → FÁCIL/MEDIO (necesitan descriptor)
- #1 "Pair Controller" falta en dashboard: probablemente resuelto por #18 → verificar
- #15 Rumble USB/XInput (GameSir Cyclone 2): parcialmente cubierto por #18/#21 → verificar
- #14 Flydigi Vader 4 Pro: mapeo click touchpad → MEDIO
- #2,#3,#4,#5,#6,#11,#13: hardware específico, requieren reportes/capturas → no tocar sin datos
- #12 ya cubierto por #17; #16 es reporte de éxito (G7), cubierto por #19
