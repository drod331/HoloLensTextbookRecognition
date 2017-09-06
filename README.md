# HoloLensTextbookRecognition
This is a (currently unfinished) research project that uses a HoloLens C++ application to detect text in textbooks and displaying a 3D rendering of the text.

The program can be broken down into four main sections
1. HoloLens Core application, which handles graphics
2. Background thread that handles voice input
3. Functions that handle hologram manipulation
4. Background thread that handles text recognition

In its current state (as of the latest commit on 9/5/2017), section 4 does not work as intended.

The application's use case can be described as follows:
1. The user looks at a textbook with the HoloLens.
2. The application looks for the word "Figure", followed by numbers and potentially special characters (think Figure 1, Figure 4.3, Figure 7-2, etc.)
3. The application displays the text, incuding numbers, as a hologram.
4. The user can then use voice commands to manipulate the hologram (rotate, scale) or to stop rendering the current hologram.

Future Steps
1. Fix Section 4 by enabling OpenCV to properly access the resources of HoloLens.
2. Ensure proper preservation of the matrix stack for graphics manipulation.
3. Ensure hologram manipulation functions work smoothly for text.
4. Tweak text recognition algorithms as necessary for higher level of accuracy.
5. Implement phase 2 of the project by replacing step 3 in the use case with a step that calls the rendering associated with the figure.

Libraries used by this project include DirectXTK, Leptonica, Tesseract, and OpenCV.
