@echo off
title Image Tunnel Manager
color 0A

echo =========================================
echo       Image Tunnel - Control Center
echo =========================================
echo.
echo Starting the Gradio Local Server...
echo Do not close this window while using the GUI.
echo.

:: Optional: If you use a virtual environment, uncomment the line below and point it to your activate script.
:: call venv\Scripts\activate.bat

:: Launch the GUI. Gradio will automatically pop open your default browser.
python gui.py

:: If the server crashes or you shut it down, pause so you can read any error messages.
pause