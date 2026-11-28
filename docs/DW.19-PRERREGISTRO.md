# DW.19 - Cinturón externo: corregir el signo de L respecto a Kp

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`352c139707796476c471e964f55368e99fa78af0`. Worktree
`ionosphere-dw19-cpp`, rama `dw.19-belt-l-sign`.

Origen: plan conjunto `PLAN-CONJUNTO.md`, item F2, veredicto CONFORME
CON NOTAS. David ha dado autonomia total a los tres agentes; se sigue
el gate pre-registro → veredicto → GO (general) → ejecucion.

## 1. Problema medido

- `src/Ionosphere/VanAllenBelts.cpp` `outerLForKp`:
  `return 4.2f + 0.08f * k;` con `k = clamp(kp, 0, 9)` → **L crece con Kp**
  (4,2 a 4,92 R_E).
- `src/Ionosphere/VanAllenBelts.h:12` y `src/App.cpp:5446` documentan lo
  mismo ("torus axis is the geographic equator, dipole tilt ignored").
- Claude (en `CLAUDE-MEJORAS.md`) senala que el signo fisico parece
  invertido: en tormentas el cinturon de electrones y la plasmapausa se
  desplazan **hacia dentro** (referencia `L_pp ≈ 5,6 − 0,46·Kp`,
  Carpenter & Anderson 1992).

## 2. Cambio propuesto (minimo)

- `outerLForKp` pasa a `L = 4.2 − 0.08 · Kp`, con `Kp` acotado 0..9 y
  `L >= 3.48` (rango 3,48..4,20 R_E). Se mantiene el parametro 0,08
  existente; solo cambia el signo, para no inventar una nueva ley.
- Actualizar los dos textos que lo documentan:
  `VanAllenBelts.h:12` y el tooltip de `App.cpp:5446`.
- Anadir test nuevo `tests/test_van_allen_belts.cpp` con `add_test` en
  `CMakeLists.txt` (no existe test de cinturones hoy):
  - `outerLForKp(0) == 4.2`;
  - `outerLForKp(5) == 3.8`;
  - `outerLForKp(9) == 3.48`;
  - **monotona decreciente** en 0..9;
  - `NaN` e infinito → 4.2 (comportamiento actual preservado);
  - `outerL()` sigue a `kp` tras `setKp`.

Se declara como **v2 parametrica**: no se afirma un ajuste empirico
nuevo; se corrige solo la direccion del efecto tormenta.

## 3. Criterio binario

1. `cmake --build` rc=0; `ctest` rc=0 con el total de la base + 1 test.
2. Test nuevo: 6 checks anteriores, todos PASS.
3. Grep: 0 coincidencias de `4.2 + 0.08` en `src/`; textos actualizados.
4. Runtime: captura del tooltip del toro externo con "L = 4.2 - 0.08*Kp"
   y L dentro de 3,48..4,20; sin cambios visuales fuera del toro.

## 4. No-goals

- No tocar el toro interno (L=1,5), la deriva de fase, el brillo RBE ni
  el eje del toro (geografico, inclinacion ignorada) — eso es F1.
- No tocar viento solar (F3), Shue, magnetopausa ni rio.
- No tocar Web/Android en este slice (heredaran la correccion cuando se
  abra su slice).
- No merge sin veredicto.

## 5. Evidencia

`git diff --numstat`, grep pre/post, salida de `ctest` y del test nuevo,
captura del tooltip, `handoff.py check` limpio. Ademas, **verificacion
del gold Shue existente**: ejecutar `test_solar_wind_river.exe` y
reportar que los pineados `Dp=1.33808→r0=10.895` y `Bz=-10→9.37` pasan,
sin crear un test duplicado (nota 3 del veredicto del plan).

## 6. Riesgos

- Al bajar L con Kp alto el toro externo se acerca al interior; se
  declara en el tooltip y se valida visualmente con Kp alto simulado.
- La direccion fisica se toma de la referencia citada por Claude; si el
  verificador quiere otra ley, se enmienda antes de ejecutar.
