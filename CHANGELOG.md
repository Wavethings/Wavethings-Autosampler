# Cambios

## 0.1.2
- Escaneo de plugins en un proceso aparte: un plugin que se cierra o se bloquea ya no cierra ni congela la app; se aparta a la lista negra (tiempo máximo 60 s por plugin) y el escaneo continúa.
- Si la app se cierra mientras carga un plugin, en el siguiente arranque no se vuelve a cargar automáticamente.
- One Shot: nuevo modo **Notas sueltas**. Se eligen las notas en el piano de la app o con el teclado MIDI (p. ej. C1, D1, E1, G1); solo se graban esas y cada una se mapea únicamente a su tecla.
- JUCE 8.0.15.

## 0.1.1
- GitHub Actions: macOS 15 (macOS 14 ya no existe en GitHub), Ubuntu 24.04, acciones con Node 24.

## 0.1
- Primera versión.
