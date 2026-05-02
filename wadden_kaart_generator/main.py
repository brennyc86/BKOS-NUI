import sys
import os

# Zorg dat de app-map in het Python path staat (ook na PyInstaller packaging)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import tkinter as tk
from gui import WaddenKaartApp


def main():
    root = tk.Tk()
    app = WaddenKaartApp(root)

    # Scherm centreren
    root.update_idletasks()
    breedte = root.winfo_reqwidth()
    hoogte  = root.winfo_reqheight()
    x = (root.winfo_screenwidth()  - breedte) // 2
    y = (root.winfo_screenheight() - hoogte)  // 2
    root.geometry(f"+{x}+{y}")

    root.mainloop()


if __name__ == "__main__":
    main()
