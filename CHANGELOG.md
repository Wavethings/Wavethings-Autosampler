# Cambios

## 0.3.1
- **Lista de plugins:** selector de tipo (Todos, Solo VST3, Solo AU, Solo LV2…), que se recuerda, y **favoritos**: la estrella junto a la lista marca el plugin cargado y los favoritos salen al principio.
- **Aprender del plugin:** el botón abre la interfaz del plugin y cada control que muevas en ella se añade al panel (sin CC). Los parámetros que se mueven solos (LFOs, medidores) se ignoran. Pulsa otra vez (o cierra la interfaz) para terminar.
- Cada control del panel tiene en «...» (o clic derecho): **asignar control MIDI** moviendo un control del teclado (también con doble clic), **elegir CC** a mano, **canal MIDI**, quitar el CC, perilla o botón y quitar del panel. Un CC mueve un solo control.

## 0.3.0
- **Instrumentos hardware.** Botón **Hardware** en la sección Instrumento: salida MIDI, canal, entrada de audio (par estéreo o mono), «Escuchar entrada» y latencia extra. Las notas van al sinte por MIDI y se graba la entrada de la interfaz, con la latencia de ida y vuelta compensada. Tu teclado MIDI toca el sinte a través de la app en el canal elegido.
- **Detector de silencio reforzado:** ninguna nota empieza mientras suena la anterior. Si el sonido no baja del umbral en la nueva **Espera máxima de silencio**, se envía All Notes Off (y pedal a 0) y se vuelve a esperar. **Medir ruido** pone el umbral 6 dB por encima del ruido de fondo. Aviso cuando la entrada satura.
- **Programas y bancos (lotes)** para plugins y hardware: rango de programas, Bank Select CC0/CC32 con sus rangos, espera tras cada cambio, «Enviar ahora» para escuchar, lista de nombres y «Omitir programas mudos». Cada programa es un instrumento completo en su carpeta. Con plugins, también por la lista de programas del plugin (con sus nombres).
- El tiempo y el tamaño estimados tienen en cuenta todos los programas del lote.
- Audio / MIDI: ahora se pueden activar entradas de audio.

## 0.2.0
- **Barra de rango sobre el teclado:** arrastra los extremos para cambiar la nota más baja y la más alta, o el centro para mover el rango entero. Un clic fuera de la barra lleva el extremo más cercano a esa tecla; doble clic, C1–C7. Marca las notas que se van a grabar.
- **Flechas a izquierda y derecha** del teclado: mueven el rango un semitono (con Mayús, una octava; mantenlas pulsadas para que repitan). En One Shot mueven todas las notas elegidas.
- **Presets de sampleo** (arriba): Piano/Keys, Pad, Pad Penrose, Lead, Bajo, Cuerdas/Metales, Pluck, Órgano, Batería One Shot (kit GM), FX/Texturas y Prueba rápida. Cargan rango, paso, capas, duración, release, pausa y loop; no tocan el plugin, el umbral ni la exportación. Puedes guardar los tuyos, sobrescribirlos y borrarlos («Guardar / borrar»).
- **Tamaño estimado** bajo el resumen: lo que ocuparán los WAV y el total con las copias de los formatos elegidos. Se actualiza con el rango, las capas, los tiempos, el loop, los bits, estéreo/mono y los formatos.
- **MPC: preescucha en el navegador.** Cada programa lleva `[Previews]/<nombre>.xpm.ogg` (Ogg Vorbis): la muestra más cercana a C3 en la capa más fuerte, hasta 6 s con el loop desplegado y un fundido final, como en Keygroup Creator. Copia a la MPC la carpeta `MPC` completa.

## 0.1.9
- Sección Notas: dos modos, **Rango** o **One Shot**. One Shot es el modo de notas sueltas y no desactiva el loop; el botón Loop decide. Sin loop, la muestra se reproduce entera (one shot en el sampler).
- MPC: corregido el tipo de crossfade, que salía al revés (en la MPC 1 = lineal, 0 = curvado).

## 0.1.8
- Loops: botón **Loop** propio y modos en inglés (Forward, Backward, Ping-Pong, Penrose).
- Botón **Crossfade** independiente con sus dos formas, lineal y curvada, dibujadas como los iconos de la MPC.
- **One Shot** pasa a la fila «Notas», junto a Rango y Notas sueltas. Notas sueltas ya no exige One Shot: se puede samplear una sola nota con loop.
- MPC: corregido el loop que aparecía desactivado en la 0.1.7 (byte de modo de loop en el bloque Akai del `smpl`).

## 0.1.7
- MPC: el X-FADE se escribe también en el chunk `smpl` de cada WAV, en el bloque extra de Akai donde la MPC lo lee (mismo formato que un WAV guardado por la MPC).
- Tipo de crossfade automático: lineal cuando las dos partes del loop se parecen (lo habitual en sonidos sostenidos) y curvado cuando no. En la MPC: icono de la izquierda (lineal) o de la derecha (curvado).

## 0.1.6
- MPC: corregido «Program file invalid». Los WAV para la MPC siguen ahora exactamente la estructura de los que guarda la MPC (fmt, atem, smpl, meta, data, todos de tamaño par) y el final de la muestra nunca apunta más allá del último sample del archivo.

## 0.1.5
- MPC 3.x: el loop y su crossfade se escriben también en el chunk `atem` de cada WAV de `_[ProgramData]`, que es donde la MPC los guarda y los lee (además del chunk `meta` con el tempo). Antes el crossfade aparecía a 0 en la MPC.
- El final del loop en el programa MPC coincide con el de la MPC (último sample del loop).

## 0.1.4
- Icono de la aplicación (macOS, Windows y Linux).
- Loops mucho más largos: el inicio y el final se buscan alrededor de los puntos elegidos (por defecto 30 % y 95 % de la nota sostenida), en lugar de quedarse con un trozo pequeño del final.
- Crossfade sin subidas de volumen: lineal para audio parecido, de potencia constante para audio distinto, o una mezcla de ambos.
- «Igualar el nivel dentro del loop»: compensa la caída de volumen dentro del loop (máx. +12 dB).
- Crossfade «en el sampler»: el WAV queda sin fundir y el valor se escribe en el parámetro de crossfade de MPC, Kontakt, Logic, Ableton, Bitwig, SFZ y Decent Sampler.
- MPC: formato moderno MPC 3.x (programa JSON + carpeta `_[ProgramData]`), creado a partir de un programa guardado por la MPC 3.10. Loop, crossfade del loop (en muestras), envolvente de amplitud con los tiempos medidos en la MPC, One Shot y capas de velocidad 0-127. El formato antiguo queda como «Akai MPC 2.x (legacy)».

## 0.1.3
- Buscador de parámetros del plugin (filtra mientras escribes; flechas + Intro para elegir).
- Tipo de control por parámetro: perilla o botón on/off (automático para parámetros de dos estados). Se cambia con clic derecho o el botón «…».
- Tamaño de la interfaz de 75 % a 200 % (se guarda).
- Modo silencio: graba sin sonar por los altavoces. Opción «Rápido»: render offline más rápido que en tiempo real.

## 0.1.2
- Escaneo de plugins en un proceso aparte: un plugin que se cierra o se bloquea ya no cierra ni congela la app; se aparta a la lista negra (tiempo máximo 60 s por plugin) y el escaneo continúa.
- Si la app se cierra mientras carga un plugin, en el siguiente arranque no se vuelve a cargar automáticamente.
- One Shot: nuevo modo **Notas sueltas**. Se eligen las notas en el piano de la app o con el teclado MIDI (p. ej. C1, D1, E1, G1); solo se graban esas y cada una se mapea únicamente a su tecla.
- JUCE 8.0.15.

## 0.1.1
- GitHub Actions: macOS 15 (macOS 14 ya no existe en GitHub), Ubuntu 24.04, acciones con Node 24.

## 0.1
- Primera versión.
