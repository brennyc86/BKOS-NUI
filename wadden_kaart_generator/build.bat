@echo off
REM Bouw WaddenKaartGenerator.exe met PyInstaller
REM Voer uit vanuit de wadden_kaart_generator map

pip install -r requirements.txt

pyinstaller --onefile --windowed ^
  --name "WaddenKaartGenerator" ^
  --add-data "config.py;." ^
  --hidden-import "contourpy" ^
  --hidden-import "pyproj" ^
  --hidden-import "tifffile" ^
  --hidden-import "PIL._tkinter_finder" ^
  main.py

echo.
echo Build klaar! Bestand staat in: dist\WaddenKaartGenerator.exe
pause
