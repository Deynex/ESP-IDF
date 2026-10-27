# Informe técnico en LaTeX

[funcionamiento_tecnico.tex](funcionamiento_tecnico.tex) describe el código actual de NAT Router Lite: arquitectura modular, eventos y reconexión, validación y NVS, DHCP/NAPT, proxy DNS, aplicación HTTP, diagnóstico, LED/RMT, perfiles por chip y límites de funcionamiento. Incluye diagramas, tablas y referencias oficiales.

## Generarlo en Overleaf

1. Crea un proyecto en blanco.
2. Sube `funcionamiento_tecnico.tex` y selecciónalo como documento principal; también puedes copiar todo su contenido en `main.tex`.
3. Selecciona **pdfLaTeX** como compilador y pulsa **Recompile**.
4. Si el índice o las referencias cruzadas aún no aparecen, compila otra vez.

El archivo es UTF-8 y autónomo: no necesita el código del repositorio, imágenes, archivos `.bib`, `minted` ni `shell-escape`. Usa paquetes habituales de TeX Live disponibles en Overleaf y genera los diagramas con TikZ.

Con una distribución LaTeX local que incluya esos paquetes, desde la raíz del repositorio también puedes ejecutar dos veces:

```powershell
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=docs docs/funcionamiento_tecnico.tex
```

Los datos concretos describen la implementación revisada con ESP-IDF 6.1. Al cambiar APIs, límites DNS, perfiles o lógica de estado, actualiza el informe junto con la [guía de uso principal](../README.md).
