// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h> // tolower()
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "pt2_textout.h"
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_visuals.h"
#include "pt2_diskop.h"
#include "pt2_edit.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_keyboard.h"
#include "pt2_tables.h"
#include "pt2_module_loader.h"
#include "pt2_module_saver.h"
#include "pt2_mouse.h"
#include "pt2_unicode.h"
#include "pt2_config.h"
#include "pt2_sampling.h"
#include "pt2_chordmaker.h"

#if defined _WIN32 && !defined _DEBUG
extern bool windowsKeyIsDown;
extern HHOOK g_hKeyboardHook;
#endif

void movePatCurPrevCh(void);
void movePatCurNextCh(void);
void movePatCurRight(void);
void movePatCurLeft(void);

static bool handleGeneralModes(SDL_Keycode keycode, SDL_Scancode scancode);
bool handleTextEditMode(SDL_Scancode scancode);

void sampleUpButton(void); // pt2_mouse.c
void sampleDownButton(void); // pt2_mouse.c

void gotoNextMulti(void)
{
	cursor.channel = (editor.multiModeNext[cursor.channel] - 1) & 3;
	cursor.pos = cursor.channel * 6;
	updateCursorPos();
}

void readKeyModifiers(void)
{
	const SDL_Keymod modState = SDL_GetModState();

	keyb.leftCtrlPressed = (modState & KMOD_LCTRL)  ? true : false;
	keyb.leftAltPressed = (modState & KMOD_LALT) ? true : false;
	keyb.shiftPressed = (modState & (KMOD_LSHIFT + KMOD_RSHIFT)) ? true : false;

#ifdef __APPLE__
	keyb.leftCommandPressed = (modState & KMOD_LGUI) ? true : false;
#endif

#if defined _WIN32 && !defined _DEBUG
	keyb.leftAmigaPressed = windowsKeyIsDown; // Windows: handled in lowLevelKeyboardProc
#else
	keyb.leftAmigaPressed = (modState & KMOD_LGUI) ? true : false;
#endif
}

#if defined _WIN32 && !defined _DEBUG
/* For taking control over the windows key if the program has focus.
** Warning: Don't do this in debug mode, it will completely ruin the keyboard input
** latency (in the OS in general) when the debugger is breaking.
*/
LRESULT CALLBACK lowLevelKeyboardProc(int32_t nCode, WPARAM wParam, LPARAM lParam)
{
	SDL_Window *window = video.window;

	if (nCode == HC_ACTION && window != NULL)
	{
		switch (wParam)
		{
			case WM_KEYUP:
			case WM_KEYDOWN:
			case WM_SYSKEYUP: // needed to prevent stuck Windows key if used with ALT
			{
				const bool windowHasFocus = SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
				if (!windowHasFocus)
				{
					windowsKeyIsDown = false;
					break;
				}

				if (((KBDLLHOOKSTRUCT *)lParam)->vkCode != VK_LWIN)
					break;

				windowsKeyIsDown = (wParam == WM_KEYDOWN);
				return 1; // eat keystroke
			}
			break;

			default: break;
		}
	}

	return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}
#endif

// these four functions are for the text edit cursor
void textMarkerMoveLeft(void)
{
	if (ui.dstPos > 0)
	{
		removeTextEditMarker();
		ui.dstPos--;
		ui.lineCurX -= FONT_CHAR_W;
		renderTextEditMarker();
	}
	else
	{
		if (ui.dstOffset != NULL)
		{
			(*ui.dstOffset)--;
			if (ui.editObject == PTB_DO_DATAPATH)
				ui.updateDiskOpPathText = true;
		}
	}
}

void textMarkerMoveRight(void)
{
	if (ui.editTextType == TEXT_EDIT_STRING)
	{
		if (ui.dstPos < ui.textLength-1)
		{
			removeTextEditMarker();
			ui.dstPos++;
			ui.lineCurX += FONT_CHAR_W;
			renderTextEditMarker();
		}
		else
		{
			if (ui.dstOffset != NULL)
			{
				(*ui.dstOffset)++;
				if (ui.editObject == PTB_DO_DATAPATH)
					ui.updateDiskOpPathText = true;
			}
		}
	}
	else
	{
		// we end up here when entering a number/hex digit

		if (ui.dstPos < ui.numLen)
			removeTextEditMarker();

		ui.dstPos++;
		ui.lineCurX += FONT_CHAR_W;

		if (ui.dstPos < ui.numLen)
			renderTextEditMarker();

		// don't clamp, dstPos is tested elsewhere to check if done editing a number
	}
}

void textCharPrevious(void)
{
	if (ui.editTextType != TEXT_EDIT_STRING)
	{
		if (ui.dstPos > 0)
		{
			removeTextEditMarker();
			ui.dstPos--;
			ui.lineCurX -= FONT_CHAR_W;
			renderTextEditMarker();
		}

		return;
	}

	if (editor.mixFlag && ui.dstPos <= 4)
		return;

	if (ui.editPos > ui.showTextPtr)
	{
		removeTextEditMarker();

		ui.editPos--;
		textMarkerMoveLeft();

		if (editor.mixFlag) // special case for "Mix" input field in Edit. Op.
		{
			if (ui.dstPos == 12)
			{
				ui.editPos--; textMarkerMoveLeft();
				ui.editPos--; textMarkerMoveLeft();
				ui.editPos--; textMarkerMoveLeft();
				ui.editPos--; textMarkerMoveLeft();
			}
			else if (ui.dstPos == 6)
			{
				ui.editPos--;
				textMarkerMoveLeft();
			}
		}

		renderTextEditMarker();
	}

	ui.dstOffsetEnd = false;
}

void textCharNext(void)
{
	if (ui.editTextType != TEXT_EDIT_STRING)
	{
		if (ui.dstPos < ui.numLen-1)
		{
			removeTextEditMarker();
			ui.dstPos++;
			ui.lineCurX += FONT_CHAR_W;
			renderTextEditMarker();
		}

		return;
	}

	if (editor.mixFlag && ui.dstPos >= 14)
		return;

	if (ui.editPos < ui.textEndPtr)
	{
		if (*ui.editPos != '\0')
		{
			removeTextEditMarker();

			ui.editPos++;
			textMarkerMoveRight();

			if (editor.mixFlag) // special case for "Mix" input field in Edit. Op.
			{
				if (ui.dstPos == 9)
				{
					ui.editPos++; textMarkerMoveRight();
					ui.editPos++; textMarkerMoveRight();
					ui.editPos++; textMarkerMoveRight();
					ui.editPos++; textMarkerMoveRight();
				}
				else if (ui.dstPos == 6)
				{
					ui.editPos++;
					textMarkerMoveRight();
				}
			}

			renderTextEditMarker();
		}
		else
		{
			ui.dstOffsetEnd = true;
		}
	}
	else
	{
		ui.dstOffsetEnd = true;
	}
}
// --------------------------------

void keyUpHandler(SDL_Scancode scancode, SDL_Keycode keycode)
{
	if (scancode == SDL_SCANCODE_KP_PLUS)
		keyb.keypadEnterPressed = false;

	if (scancode == keyb.lastRepKey)
		keyb.lastRepKey = SDL_SCANCODE_UNKNOWN;

	switch (scancode)
	{
		// modifiers shouldn't reset keyb repeat/delay flags & counters
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL:
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT:
		case SDL_SCANCODE_LALT:
		case SDL_SCANCODE_RALT:
		case SDL_SCANCODE_LGUI:
		case SDL_SCANCODE_RGUI:
		case SDL_SCANCODE_MENU:
		case SDL_SCANCODE_MODE:
		case SDL_SCANCODE_CAPSLOCK:
		break;

		default:
		{
			keyb.repeatKey = false;
			keyb.delayKey = false;
			keyb.repeatFrac = 0;
			keyb.delayCounter = 0;
		}
		break;
	}

	(void)keycode;
}

static void incMulti(uint8_t slot)
{
	char str[32];

	assert(slot < 4);
	if (editor.multiModeNext[slot] == 4)
		editor.multiModeNext[slot] = 1;
	else
		editor.multiModeNext[slot]++;

	sprintf(str, "MULTI=%d-%d-%d-%d", editor.multiModeNext[0], editor.multiModeNext[1],
		editor.multiModeNext[2], editor.multiModeNext[3]);
	displayMsg(str);
}

void keyDownHandler(SDL_Scancode scancode, SDL_Keycode keycode)
{
	uint8_t blockFrom, blockTo;
	int16_t i, j;
	note_t *patt, *noteSrc, *noteDst, noteTmp;
	moduleSample_t *s;
	moduleChannel_t *ch;

	if (scancode == SDL_SCANCODE_CAPSLOCK)
	{
		editor.repeatKeyFlag ^= 1;
		return;
	}

	// kludge: allow certain specific key combos to be repeated with the ctrl key
	const bool nonRepeatAltKeys = keyb.leftAltPressed && scancode != SDL_SCANCODE_DELETE    && scancode != SDL_SCANCODE_RETURN
	                                                  && scancode != SDL_SCANCODE_BACKSPACE && scancode != SDL_SCANCODE_BACKSLASH
	                                                  && scancode != SDL_SCANCODE_EQUALS    && scancode != SDL_SCANCODE_MINUS
	                                                  && scancode <  SDL_SCANCODE_1         && scancode >  SDL_SCANCODE_0;

	// kludge: allow certain specific key combos to be repeated with the alt key
	const bool nonRepeatCtrlKeys = keyb.leftCtrlPressed && scancode != SDL_SCANCODE_DELETE && scancode != SDL_SCANCODE_RETURN
	                                                    && scancode != SDL_SCANCODE_BACKSPACE;

	// these keys should not allow to be repeated in keyrepeat mode (caps lock)
	const bool nonRepeatKeys = keyb.leftAmigaPressed || nonRepeatAltKeys || nonRepeatCtrlKeys
		|| scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT
		|| scancode == SDL_SCANCODE_UP   || scancode == SDL_SCANCODE_DOWN;
	if (editor.repeatKeyFlag && keyb.repeatKey && scancode == keyb.lastRepKey && nonRepeatKeys)
		return;

	if (scancode == SDL_SCANCODE_KP_PLUS)
		keyb.keypadEnterPressed = true;

	// TOGGLE FULLSCREEN (should always react)
	if (scancode == SDL_SCANCODE_F11 && !keyb.leftAltPressed)
	{
		toggleFullScreen();

		// prevent fullscreen toggle from firing twice on certain SDL2 Linux ports
#ifdef __unix__
		SDL_Delay(100);
#endif
		return;
	}

	// don't handle input if an error message wait is active or if an unknown key is passed
	if ((editor.errorMsgActive && editor.errorMsgBlock) || scancode == SDL_SCANCODE_UNKNOWN)
		return;

	// if no ALT/SHIFT/CTRL/AMIGA, update last key for repeat routine
	if (scancode != SDL_SCANCODE_LALT     && scancode != SDL_SCANCODE_RALT   &&
		scancode != SDL_SCANCODE_LCTRL    && scancode != SDL_SCANCODE_RCTRL  &&
		scancode != SDL_SCANCODE_LSHIFT   && scancode != SDL_SCANCODE_RSHIFT &&
		scancode != SDL_SCANCODE_LGUI     && scancode != SDL_SCANCODE_RGUI   &&
		scancode != SDL_SCANCODE_MENU     && scancode != SDL_SCANCODE_MODE   &&
		scancode != SDL_SCANCODE_CAPSLOCK && scancode != SDL_SCANCODE_ESCAPE)
	{
		if (editor.repeatKeyFlag)
		{
			// if Repeat Flag, repeat all keys
			if (!keyb.repeatKey)
			{
				keyb.repeatCounter = 0;
				keyb.repeatFrac = 0;
			}

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}

		keyb.repeatCounter = 0;
		keyb.repeatFrac = 0;
		keyb.lastRepKey = scancode;
	}

	// ENTRY JUMPING IN DISK OP. FILELIST
	if (ui.diskOpScreenShown && keyb.shiftPressed && !ui.editTextFlag)
	{
		if (keycode >= 32 && keycode <= 126)
		{
			handleEntryJumping(keycode);
			return;
		}
	}

	// XXX: This really needs some refactoring, it's messy and not logical

	if (!handleGeneralModes(keycode, scancode)) return;
	if (!handleTextEditMode(scancode)) return;
	if (ui.samplerVolBoxShown || ui.samplingBoxShown) return;

	if (ui.samplerFiltersBoxShown)
	{
		handleEditKeys(scancode, EDIT_NORMAL);
		return;
	}

	// GENERAL KEYS
	switch (scancode)
	{
		case SDL_SCANCODE_NONUSBACKSLASH: turnOffVoices(); break; // magic "kill all voices" button

		case SDL_SCANCODE_APOSTROPHE:
		{
			if (editor.autoInsFlag)
			{
				if (keyb.shiftPressed)
					editor.autoInsSlot -= 4;
				else
					editor.autoInsSlot--;

				if (editor.autoInsSlot < 0)
					editor.autoInsSlot = 0;

				ui.updateTrackerFlags = true;
			}
		}
		break;

		case SDL_SCANCODE_BACKSLASH:
		{
			if (keyb.leftAltPressed)
			{
				if (handleSpecialKeys(scancode) && editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 0x3F);
			}
			else
			{
				if (editor.autoInsFlag)
				{
					if (keyb.shiftPressed)
						editor.autoInsSlot += 4;
					else
						editor.autoInsSlot++;

					if (editor.autoInsSlot > 9)
						editor.autoInsSlot = 9;
				}
				else
				{
					editor.pNoteFlag = (editor.pNoteFlag + 1) % 3;
				}

				ui.updateTrackerFlags = true;
			}
		}
		break;

#ifdef __APPLE__
		case SDL_SCANCODE_RGUI:
#else
		case SDL_SCANCODE_RALT:
#endif
		{
			// right Amiga key on Amiga keyb
			if (!ui.askScreenShown)
			{
				editor.playMode = PLAY_MODE_NORMAL;
				modPlay(DONT_SET_PATTERN, song->currOrder, DONT_SET_ROW);
				editor.currMode = MODE_PLAY;
				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
				statusAllRight();
			}
		}
		break;

#ifdef __APPLE__
		case SDL_SCANCODE_RALT:
#else
		case SDL_SCANCODE_RCTRL:
#endif
		{
			// right alt on Amiga keyb
			if (!ui.askScreenShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;
				modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
				editor.currMode = MODE_PLAY;
				pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_RSHIFT:
		{
			// right shift on Amiga keyb
			if (!ui.samplerScreenShown && !ui.askScreenShown)
			{
				editor.playMode = PLAY_MODE_PATTERN;
				modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
				editor.currMode = MODE_RECORD;
				pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
				statusAllRight();
			}
		}
		break;

		case SDL_SCANCODE_ESCAPE:
		{
			if (ui.posEdScreenShown)
			{
				ui.posEdScreenShown = false;
				displayMainScreen();
			}
			else if (ui.diskOpScreenShown)
			{
				ui.diskOpScreenShown = false;
				displayMainScreen();
			}
			else if (ui.samplerScreenShown)
			{
				exitFromSam();
			}
			else if (ui.editOpScreenShown)
			{
				ui.editOpScreenShown = false;
				displayMainScreen();
			}
			else
			{
				ui.askScreenShown = true;
				ui.askScreenType = ASK_QUIT;

				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("REALLY QUIT ?", NO_CARRY);
				renderAskDialog();
				return;
			}

			pointerSetPreviousMode();
			setPrevStatusMessage();
		}
		break;

		case SDL_SCANCODE_INSERT:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamPaste();
				return;
			}
		}
		break;

		case SDL_SCANCODE_PAGEUP:
		{
			if (ui.posEdScreenShown)
			{
				if (song->currOrder > 0)
				{
					if (song->currOrder-(POSED_LIST_SIZE-1) > 0)
						modSetPos(song->currOrder-(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(0, DONT_SET_ROW);
				}
			}
			else if (ui.diskOpScreenShown)
			{
				diskop.scrollOffset -= DISKOP_LINES - 1;
				if (diskop.scrollOffset < 0)
					diskop.scrollOffset = 0;

				ui.updateDiskOpFileList = true;
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					if (song->currRow == 63)
						modSetPos(DONT_SET_ORDER, song->currRow - 15);
					else if (song->currRow == 15)
						modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
					else
						modSetPos(DONT_SET_ORDER, song->currRow - 16);
				}
			}

			if (!keyb.repeatKey)
				keyb.delayCounter = 0;

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}
		break;

		case SDL_SCANCODE_PAGEDOWN:
		{
			if (ui.posEdScreenShown)
			{
				if (song->currOrder != song->header.numOrders-1)
				{
					if (song->currOrder+(POSED_LIST_SIZE-1) <= song->header.numOrders-1)
						modSetPos(song->currOrder+(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(song->header.numOrders - 1, DONT_SET_ROW);
				}
			}
			else if (ui.diskOpScreenShown)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset += DISKOP_LINES-1;
					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, song->currRow + 16);
			}

			if (!keyb.repeatKey)
				keyb.delayCounter = 0;

			keyb.repeatKey = true;
			keyb.delayKey = true;
		}
		break;

		case SDL_SCANCODE_HOME:
		{
			if (ui.posEdScreenShown)
			{
				if (song->currOrder > 0)
					modSetPos(0, DONT_SET_ROW);
			}
			else if (ui.diskOpScreenShown)
			{
				if (diskop.scrollOffset != 0)
				{
					diskop.scrollOffset = 0;
					ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, 0);
			}
		}
		break;

		case SDL_SCANCODE_END:
		{
			if (ui.posEdScreenShown)
			{
				modSetPos(song->header.numOrders - 1, DONT_SET_ROW);
			}
			else if (ui.diskOpScreenShown)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset = diskop.numEntries - DISKOP_LINES;
					ui.updateDiskOpFileList = true;
				}
			}
			else
			{
				if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
					modSetPos(DONT_SET_ORDER, 63);
			}
		}
		break;

		case SDL_SCANCODE_DELETE:
		{
			if (ui.samplerScreenShown)
				samplerSamDelete(NO_SAMPLE_CUT);
			else
				handleEditKeys(scancode, EDIT_NORMAL);
		}
		break;

		case SDL_SCANCODE_F12:
		{
			if (keyb.leftCtrlPressed)
			{
				const bool audioWasntLocked = !audio.locked;
				if (audioWasntLocked)
					lockAudio();

				editor.timingMode ^= 1;
				updateReplayerTimingMode();

				if (editor.timingMode == TEMPO_MODE_VBLANK)
				{
					editor.oldTempo = song->currBPM;
					modSetTempo(125, false);
				}
				else
				{
					modSetTempo(editor.oldTempo, false);
				}

				if (audioWasntLocked)
					unlockAudio();

				ui.updateSongTiming = true;
			}
			else if (keyb.shiftPressed)
			{
				toggleAmigaPanMode();
			}
			else
			{
				toggleFilterModel();
			}
		}
		break;

		case SDL_SCANCODE_RETURN:
		{
			if (ui.askScreenShown)
			{
				ui.answerNo = false;
				ui.answerYes = true;
				ui.askScreenShown = false;

				handleAskYes();
			}
			else
			{
				if (keyb.shiftPressed || keyb.leftAltPressed || keyb.leftCtrlPressed)
				{
					saveUndo();
					if (keyb.leftAltPressed && !keyb.leftCtrlPressed)
					{
						if (song->currRow < 63)
						{
							for (i = 0; i < AMIGA_VOICES; i++)
							{
								for (j = 62; j >= song->currRow; j--)
								{
									noteSrc = &song->patterns[song->currPattern][(j * AMIGA_VOICES) + i];
									song->patterns[song->currPattern][((j + 1) * AMIGA_VOICES) + i] = *noteSrc;
								}

								noteDst = &song->patterns[song->currPattern][((j + 1) * AMIGA_VOICES) + i];

								noteDst->period = 0;
								noteDst->sample = 0;
								noteDst->command = 0;
								noteDst->param = 0;
							}

							song->currRow++;

							updateWindowTitle(MOD_IS_MODIFIED);
							ui.updatePatternData = true;
						}
					}
					else
					{
						if (song->currRow < 63)
						{
							for (i = 62; i >= song->currRow; i--)
							{
								noteSrc = &song->patterns[song->currPattern][((i + 0) * AMIGA_VOICES) + cursor.channel];
								noteDst = &song->patterns[song->currPattern][((i + 1) * AMIGA_VOICES) + cursor.channel];

								if (keyb.leftCtrlPressed)
								{
									noteDst->command = noteSrc->command;
									noteDst->param = noteSrc->param;
								}
								else
								{
									*noteDst = *noteSrc;
								}
							}

							noteDst = &song->patterns[song->currPattern][((i + 1) * AMIGA_VOICES) + cursor.channel];

							if (!keyb.leftCtrlPressed)
							{
								noteDst->period = 0;
								noteDst->sample = 0;
							}

							noteDst->command = 0;
							noteDst->param = 0;

							song->currRow++;

							updateWindowTitle(MOD_IS_MODIFIED);
							ui.updatePatternData = true;
						}
					}
				}
				else
				{
					editor.stepPlayEnabled = true;
					editor.stepPlayBackwards = false;

					editor.stepPlayLastMode = editor.currMode;

					if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
						doStopIt(false);
					else
						doStopIt(true);

					playPattern(song->currRow);

					if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
					{
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						editor.playMode = PLAY_MODE_NORMAL;
						editor.currMode = MODE_EDIT;
					}
				}
			}
		}
		break;

		// toggle between IDLE and EDIT (IDLE if PLAY)
		case SDL_SCANCODE_SPACE:
		{
			if (editor.currMode == MODE_PLAY)
			{
				modStop();
				editor.currMode = MODE_IDLE;
				pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
				statusAllRight();
			}
			else if (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD)
			{
				if (!ui.samplerScreenShown)
				{
					modStop();
					editor.currMode = MODE_IDLE;
					pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
					statusAllRight();
				}
			}
			else
			{
				if (!ui.samplerScreenShown)
				{
					modStop();
					editor.currMode = MODE_EDIT;
					pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
					statusAllRight();
				}
			}
		}
		break;

		case SDL_SCANCODE_F1: editor.keyOctave = OCTAVE_LOW;  break;
		case SDL_SCANCODE_F2: editor.keyOctave = OCTAVE_HIGH; break;

		case SDL_SCANCODE_F3:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamDelete(SAMPLE_CUT);
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// cut channel and put in buffer
					saveUndo();

					noteDst = editor.trackBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteSrc = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						*noteDst++ = *noteSrc;

						noteSrc->period = 0;
						noteSrc->sample = 0;
						noteSrc->command = 0;
						noteSrc->param = 0;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftAltPressed)
				{
					// cut pattern and put in buffer
					saveUndo();

					memcpy(editor.patternBuffer, song->patterns[song->currPattern],
						sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

					memset(song->patterns[song->currPattern], 0,
						sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftCtrlPressed)
				{
					// cut channel commands and put in buffer
					saveUndo();

					noteDst = editor.cmdsBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteSrc = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						*noteDst++ = *noteSrc;

						noteSrc->command = 0;
						noteSrc->param = 0;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
			}
		}
		break;

		case SDL_SCANCODE_F4:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamCopy();
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// copy channel to buffer

					noteDst = editor.trackBuffer;
					for (i = 0; i < MOD_ROWS; i++)
						*noteDst++ = song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
				}
				else if (keyb.leftAltPressed)
				{
					// copy pattern to buffer

					memcpy(editor.patternBuffer, song->patterns[song->currPattern],
						sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));
				}
				else if (keyb.leftCtrlPressed)
				{
					// copy channel commands to buffer

					noteDst = editor.cmdsBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteSrc = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						noteDst->command = noteSrc->command;
						noteDst->param = noteSrc->param;

						noteDst++;
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_F5:
		{
			if (ui.samplerScreenShown)
			{
				samplerSamPaste();
			}
			else
			{
				if (keyb.shiftPressed)
				{
					// paste channel buffer to channel
					saveUndo();

					noteSrc = editor.trackBuffer;
					for (i = 0; i < MOD_ROWS; i++)
						song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel] = *noteSrc++;

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftAltPressed)
				{
					// paste pattern buffer to pattern
					saveUndo();

					memcpy(song->patterns[song->currPattern],
						editor.patternBuffer, sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
				else if (keyb.leftCtrlPressed)
				{
					// paste channel commands buffer to channel
					saveUndo();

					noteSrc = editor.cmdsBuffer;
					for (i = 0; i < MOD_ROWS; i++)
					{
						noteDst = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						noteDst->command = noteSrc->command;
						noteDst->param = noteSrc->param;

						noteSrc++;
					}

					updateWindowTitle(MOD_IS_MODIFIED);
					ui.updatePatternData = true;
				}
			}
		}
		break;

		case SDL_SCANCODE_F6:
		{
			if (keyb.shiftPressed)
			{
				editor.f6Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f6Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f6Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currOrder, editor.f6Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f6Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F7:
		{
			if (keyb.shiftPressed)
			{
				editor.f7Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f7Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f7Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currOrder, editor.f7Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f7Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F8:
		{
			if (keyb.shiftPressed)
			{
				editor.f8Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f8Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f8Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currOrder, editor.f8Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f8Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F9:
		{
			if (keyb.shiftPressed)
			{
				editor.f9Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f9Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f9Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currOrder, editor.f9Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f9Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F10:
		{
			if (keyb.shiftPressed)
			{
				editor.f10Pos = song->currRow;
				displayMsg("POSITION SET");
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.playMode = PLAY_MODE_PATTERN;
					modPlay(song->currPattern, DONT_SET_ORDER, editor.f10Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (keyb.leftCtrlPressed)
				{
					if (!ui.samplerScreenShown)
					{
						editor.playMode = PLAY_MODE_PATTERN;
						modPlay(song->currPattern, DONT_SET_ORDER, editor.f10Pos);

						editor.currMode = MODE_RECORD;
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						statusAllRight();
					}
				}
				else if (keyb.leftAmigaPressed)
				{
					editor.playMode = PLAY_MODE_NORMAL;
					modPlay(DONT_SET_PATTERN, song->currOrder, editor.f10Pos);

					editor.currMode = MODE_PLAY;
					pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
					statusAllRight();
				}
				else if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					modSetPos(DONT_SET_ORDER, editor.f10Pos);
				}
			}
		}
		break;

		case SDL_SCANCODE_F11:
		{
			if (keyb.leftAltPressed)
			{
				config.realVuMeters ^= 1;
				displayMsg(config.realVuMeters ? "VU-METERS: REAL" : "VU-METERS: FAKE");
			}
		}
		break;

		case SDL_SCANCODE_TAB:
		{
			if (keyb.shiftPressed)
				movePatCurPrevCh();
			else
				movePatCurNextCh();
		}
		break;

		case SDL_SCANCODE_0:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 0;
				displayMsg("EDITSKIP = 0");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[9] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_1:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 1;
				displayMsg("EDITSKIP = 1");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[0] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(0);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_2:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 2;
				displayMsg("EDITSKIP = 2");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[1] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(1);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_3:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 3;
				displayMsg("EDITSKIP = 3");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[2] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(2);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_4:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 4;
				displayMsg("EDITSKIP = 4");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[3] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else if (editor.currMode == MODE_IDLE && keyb.leftAltPressed)
			{
				incMulti(3);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_5:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 5;
				displayMsg("EDITSKIP = 5");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[4] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_6:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 6;
				displayMsg("EDITSKIP = 6");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[5] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_7:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 7;
				displayMsg("EDITSKIP = 7");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[6] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_8:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 8;
				displayMsg("EDITSKIP = 8");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[7] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_9:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.editMoveAdd = 9;
				displayMsg("EDITSKIP = 9");
				ui.updateTrackerFlags = true;
			}
			else if (keyb.shiftPressed)
			{
				noteSrc = &song->patterns[song->currPattern][(song->currRow * AMIGA_VOICES) + cursor.channel];
				editor.effectMacros[8] = (noteSrc->command << 8) | noteSrc->param;
				displayMsg("COMMAND STORED!");
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_KP_0:
		{
			if (editor.hiLowInstr >= 0x10)
			{
				editor.sampleZero = false;
				editor.currSample = 0x10-1;
			}
			else
			{
				editor.sampleZero = true;
				editor.currSample = 0x00;
			}

			updateCurrSample();
		}
		break;

		case SDL_SCANCODE_KP_1:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 12;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_2:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 13;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_3:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 14;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_4:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 8;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_5:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 9;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_6:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 10;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_7:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 4;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_8:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 5;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_9:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 6;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_ENTER:
		{
			if (ui.askScreenShown)
			{
				ui.answerNo = false;
				ui.answerYes = true;
				ui.askScreenShown = false;
				handleAskYes();
			}
			else
			{
				editor.hiLowInstr ^= 0x10;

				if (editor.sampleZero)
				{
					editor.currSample = 15;
					editor.sampleZero = false;
				}
				else
				{
					editor.currSample ^= 0x10;
				}

				if (editor.currSample == 31) // kludge if sample was 15 (0010 in UI) before key press
				{
					editor.currSample = 15;
					editor.sampleZero ^= 1;
				}

				updateCurrSample();
				if (keyb.leftAltPressed && editor.pNoteFlag > 0)
				{
					ui.changingDrumPadNote = true;
					setStatusMessage("SELECT NOTE", NO_CARRY);
					pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
					break;
				}

				if (editor.pNoteFlag > 0)
					handleEditKeys(scancode, EDIT_SPECIAL);
			}
		}
		break;

		case SDL_SCANCODE_KP_PLUS:
		{
			editor.sampleZero = false;

			// the Amiga numpad has one more key, so we need to use this key for two sample numbers...
			if (editor.keypadToggle8CFlag)
				editor.currSample = editor.hiLowInstr + (0x0C - 1);
			else
				editor.currSample = editor.hiLowInstr + (0x08 - 1);

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
				displayErrorMsg("INVALID PAD KEY !");

			editor.keypadToggle8CFlag ^= 1;
		}
		break;

		case SDL_SCANCODE_KP_MINUS:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 3;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_MULTIPLY:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 2;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_DIVIDE:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 1;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;
				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_NUMLOCKCLEAR:
		{
			editor.sampleZero = false;
			editor.currSample = editor.hiLowInstr + 0;

			updateCurrSample();
			if (keyb.leftAltPressed && editor.pNoteFlag > 0)
			{
				ui.changingDrumPadNote = true;

				setStatusMessage("SELECT NOTE", NO_CARRY);
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

				break;
			}

			if (editor.pNoteFlag > 0)
				handleEditKeys(scancode, EDIT_SPECIAL);
		}
		break;

		case SDL_SCANCODE_KP_PERIOD:
		{
			ui.askScreenShown = true;
			ui.askScreenType = ASK_KILL_SAMPLE;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("KILL SAMPLE ?", NO_CARRY);
			renderAskDialog();
		}
		break;

		case SDL_SCANCODE_DOWN:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (ui.diskOpScreenShown)
			{
				if (diskop.numEntries > DISKOP_LINES)
				{
					diskop.scrollOffset++;
					if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
						diskop.scrollOffset += 3;

					if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
						diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

					ui.updateDiskOpFileList = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else if (ui.posEdScreenShown)
			{
				if (song->currOrder != song->header.numOrders-1)
				{
					if (++song->currOrder > song->header.numOrders-1)
						song->currOrder = song->header.numOrders-1;

					modSetPos(song->currOrder, DONT_SET_ROW);
					ui.updatePosEd = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;
			}
			else if (!ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
					modSetPos(DONT_SET_ORDER, (song->currRow + 1) & 0x3F);

				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_UP:
		{
			keyb.delayKey  = false;
			keyb.repeatKey = false;

			if (ui.diskOpScreenShown)
			{
				diskop.scrollOffset--;
				if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
					diskop.scrollOffset -= 3;

				if (diskop.scrollOffset < 0)
					diskop.scrollOffset = 0;

				ui.updateDiskOpFileList = true;

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else if (ui.posEdScreenShown)
			{
				if (song->currOrder > 0)
				{
					modSetPos(song->currOrder - 1, DONT_SET_ROW);
					ui.updatePosEd = true;
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;
			}
			else if (!ui.samplerScreenShown)
			{
				if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
					modSetPos(DONT_SET_ORDER, (song->currRow - 1) & 0x3F);

				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (keyb.leftCtrlPressed)
			{
				sampleDownButton();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else if (keyb.shiftPressed)
			{
				if (song->currOrder > 0)
				{
					modSetPos(song->currOrder - 1, DONT_SET_ROW);
					if (editor.repeatKeyFlag)
					{
						keyb.delayKey = true;
						keyb.repeatKey = true;
					}
				}
			}
			else if (keyb.leftAltPressed)
			{
				decPatt();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else
			{
				movePatCurLeft();
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_RIGHT:
		{
			keyb.delayKey = false;
			keyb.repeatKey = false;

			if (keyb.leftCtrlPressed)
			{
				sampleUpButton();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else if (keyb.shiftPressed)
			{
				if (song->currOrder < 126)
				{
					modSetPos(song->currOrder + 1, DONT_SET_ROW);
					if (editor.repeatKeyFlag)
					{
						keyb.delayKey = true;
						keyb.repeatKey = true;
					}
				}
			}
			else if (keyb.leftAltPressed)
			{
				incPatt();
				if (editor.repeatKeyFlag)
				{
					keyb.delayKey = true;
					keyb.repeatKey = true;
				}
			}
			else
			{
				movePatCurRight();
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_A:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (ui.samplerScreenShown)
				{
					samplerRangeAll();
				}
				else
				{
					if (keyb.shiftPressed)
					{
						editor.muted[0] = true;
						editor.muted[1] = true;
						editor.muted[2] = true;
						editor.muted[3] = true;

						editor.muted[cursor.channel] = false;
						renderMuteButtons();
						break;
					}

					editor.muted[cursor.channel] ^= 1;
					renderMuteButtons();
				}
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_B:
		{
			if (keyb.leftCtrlPressed)
			{
				// CTRL+B doesn't change the status message back, so do this:
				if (ui.introScreenShown)
				{
					ui.introScreenShown = false;
					statusAllRight();
				}

				if (editor.blockMarkFlag)
				{
					editor.blockMarkFlag = false;
				}
				else
				{
					editor.blockMarkFlag = true;
					editor.blockFromPos = song->currRow;
					editor.blockToPos = song->currRow;
				}

				ui.updateStatusText = true;
			}
			else if (keyb.leftAltPressed)
			{
				s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					break;
				}

				boostSample(editor.currSample, true);

				if (ui.samplerScreenShown)
					displaySample();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_C:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (ui.samplerScreenShown)
				{
					samplerSamCopy();
					return;
				}

				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				editor.blockMarkFlag = false;
				editor.blockBufferFlag = true;

				for (i = 0; i < MOD_ROWS; i++)
					editor.blockBuffer[i] = song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];

				if (editor.blockFromPos > editor.blockToPos)
				{
					editor.buffFromPos = editor.blockToPos;
					editor.buffToPos = editor.blockFromPos;
				}
				else
				{
					editor.buffFromPos = editor.blockFromPos;
					editor.buffToPos = editor.blockToPos;
				}

				statusAllRight();
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.muted[2] ^= 1; // toggle channel 3
					renderMuteButtons();
				}
				else
				{
					handleEditKeys(scancode, EDIT_NORMAL);
				}
			}
		}
		break;

		case SDL_SCANCODE_D:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					if (!ui.posEdScreenShown)
					{
						editor.blockMarkFlag = false;

						ui.diskOpScreenShown ^= 1;
						if (!ui.diskOpScreenShown)
						{
							pointerSetPreviousMode();
							setPrevStatusMessage();
							displayMainScreen();
						}
						else
						{
							ui.diskOpScreenShown = true;
							renderDiskOpScreen();
						}
					}
				}
				else
				{
					handleEditKeys(scancode, EDIT_NORMAL);
				}
			}
		}
		break;

		case SDL_SCANCODE_E:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftAltPressed)
			{
				if (!ui.diskOpScreenShown && !ui.posEdScreenShown)
				{
					if (ui.editOpScreenShown)
						ui.editOpScreen = (ui.editOpScreen + 1) % 3;
					else
						ui.editOpScreenShown = true;

					renderEditOpScreen();
				}
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();

				j = song->currRow + 1;
				while (j < MOD_ROWS)
				{
					for (i = 62; i >= j; i--)
					{
						noteSrc = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						song->patterns[song->currPattern][((i + 1) * AMIGA_VOICES) + cursor.channel] = *noteSrc;
					}

					noteDst = &song->patterns[song->currPattern][((i + 1) * AMIGA_VOICES) + cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;

					j += 2;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_F:
		{
#ifdef __APPLE__
			if (keyb.leftCommandPressed && keyb.leftCtrlPressed)
			{
				toggleFullScreen();
			}
			else
#endif
			if (keyb.leftAmigaPressed)
			{
				pattOctaUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				toggleLEDFilter();

				if (editor.useLEDFilter)
					displayMsg("LED FILTER ON");
				else
					displayMsg("LED FILTER OFF");
			}
			else if (keyb.leftAltPressed)
			{
				s = &song->samples[editor.currSample];
				if (s->length == 0)
				{
					statusSampleIsEmpty();
					break;
				}

				filterSample(editor.currSample, true);
				if (ui.samplerScreenShown)
					displaySample();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_G:
		{
			if (keyb.leftCtrlPressed)
			{
				ui.askScreenShown = true;
				ui.askScreenType = ASK_BOOST_ALL_SAMPLES;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("BOOST ALL SAMPLES", NO_CARRY);
				renderAskDialog();
			}
			else if (keyb.leftAltPressed) // toggle record mode (PT clone and PT2.3E only)
			{
				editor.recordMode ^= 1;
				if (editor.recordMode == 0)
					displayMsg("REC MODE: PATT");
				else
					displayMsg("REC MODE: SONG");

				if (ui.editOpScreenShown && ui.editOpScreen == 1)
					ui.updateRecordText = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_H:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				trackNoteUp(TRANSPOSE_ALL, editor.blockFromPos, editor.blockToPos);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_I:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				if (song->currRow < 63)
				{
					for (i = 0; i <= editor.buffToPos-editor.buffFromPos; i++)
					{
						for (j = 62; j >= song->currRow; j--)
						{
							noteSrc = &song->patterns[song->currPattern][(j * AMIGA_VOICES) + cursor.channel];
							song->patterns[song->currPattern][((j + 1) * AMIGA_VOICES) + cursor.channel] = *noteSrc;
						}
					}
				}

				saveUndo();
				for (i = 0; i <= editor.buffToPos-editor.buffFromPos; i++)
				{
					if (song->currRow+i > 63)
						break;

					song->patterns[song->currPattern][((song->currRow + i) * AMIGA_VOICES) + cursor.channel]
						= editor.blockBuffer[editor.buffFromPos + i];
				}

				if (!keyb.shiftPressed)
				{
					song->currRow += i & 0xFF;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				editor.autoInsFlag ^= 1;
				ui.updateTrackerFlags = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_J:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				saveUndo();

				i = editor.buffFromPos;
				j = song->currRow;
				patt = song->patterns[song->currPattern];
				while (true)
				{
					noteDst = &patt[(j * AMIGA_VOICES) + cursor.channel];

					if (editor.blockBuffer[i].period == 0 && editor.blockBuffer[i].sample == 0)
					{
						noteDst->command = editor.blockBuffer[i].command;
						noteDst->param = editor.blockBuffer[i].param;
					}
					else
					{
						*noteDst = editor.blockBuffer[i];
					}

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
				}

				if (!keyb.shiftPressed)
				{
					song->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_K:
		{
			if (keyb.leftAltPressed)
			{
				for (i = 0; i < MOD_ROWS; i++)
				{
					noteSrc = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
					if (noteSrc->sample == editor.currSample+1)
					{
						noteSrc->period = 0;
						noteSrc->sample = 0;
						noteSrc->command = 0;
						noteSrc->param = 0;
					}
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftCtrlPressed)
			{
				saveUndo();

				i = song->currRow;
				if (keyb.shiftPressed)
				{
					// kill to start
					while (i >= 0)
					{
						noteDst = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						noteDst->period = 0;
						noteDst->sample = 0;
						noteDst->command = 0;
						noteDst->param = 0;

						i--;
					}
				}
				else
				{
					// kill to end
					while (i < MOD_ROWS)
					{
						noteDst = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
						noteDst->period = 0;
						noteDst->sample = 0;
						noteDst->command = 0;
						noteDst->param = 0;

						i++;
					}
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_L:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				trackNoteDown(TRANSPOSE_ALL, editor.blockFromPos, editor.blockToPos);
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_M:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.multiFlag ^= 1;
				ui.updateTrackerFlags = true;
				ui.updateKeysText = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (keyb.shiftPressed)
					editor.metroChannel = cursor.channel + 1;
				else
					editor.metroFlag ^= 1;

				ui.updateTrackerFlags = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_N:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.blockMarkFlag = true;
				song->currRow = editor.blockToPos;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_O:
		{
			if (keyb.leftCtrlPressed)
			{
				// fun fact: this function is broken in PT but I fixed it in my clone

				saveUndo();

				j = song->currRow + 1;
				while (j < MOD_ROWS)
				{
					for (i = j; i < MOD_ROWS-1; i++)
					{
						noteSrc = &song->patterns[song->currPattern][((i + 1) * AMIGA_VOICES) + cursor.channel];
						song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel] = *noteSrc;
					}

					// clear newly made row on very bottom
					noteDst = &song->patterns[song->currPattern][(63 * AMIGA_VOICES) + cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;

					j++;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_P:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				saveUndo();

				i = editor.buffFromPos;
				j = song->currRow;
				patt = song->patterns[song->currPattern];
				while (true)
				{
					noteDst = &patt[(j * AMIGA_VOICES) + cursor.channel];
					*noteDst = editor.blockBuffer[i];

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
				}

				if (!keyb.shiftPressed)
				{
					song->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				if (!ui.diskOpScreenShown)
				{
					ui.posEdScreenShown ^= 1;
					if (ui.posEdScreenShown)
					{
						renderPosEdScreen();
						ui.updatePosEd = true;
					}
					else
					{
						displayMainScreen();
					}
				}
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_Q:
		{
			if (keyb.leftAmigaPressed)
			{
				trackNoteDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.muted[0] = false;
				editor.muted[1] = false;
				editor.muted[2] = false;
				editor.muted[3] = false;
				renderMuteButtons();
			}
			else if (keyb.leftAltPressed)
			{
				ui.askScreenShown = true;
				ui.askScreenType = ASK_QUIT;

				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("REALLY QUIT ?", NO_CARRY);
				renderAskDialog();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_R:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				editor.f6Pos = 0;
				editor.f7Pos = 16;
				editor.f8Pos = 32;
				editor.f9Pos = 48;
				editor.f10Pos = 63;

				displayMsg("POS RESTORED !");
			}
			else if (keyb.leftAltPressed)
			{
				ui.askScreenShown = true;
				ui.askScreenType = ASK_RESAMPLE;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("RESAMPLE?", NO_CARRY);
				renderAskDialog();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_S:
		{
			if (keyb.leftCtrlPressed)
			{
				// if we're in sample load/save mode, set current dir to modules path
				if (diskop.mode == DISKOP_MODE_SMP)
					UNICHAR_CHDIR(editor.modulesPathU);

				saveModule(DONT_CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);

				// set current dir to samples path
				if (diskop.mode == DISKOP_MODE_SMP)
					UNICHAR_CHDIR(editor.samplesPathU);
			}
			else if (keyb.leftAmigaPressed)
			{
				pattOctaUp(TRANSPOSE_ALL);
			}
			else if (keyb.leftAltPressed)
			{
				samplerScreen();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_T:
		{
			if (keyb.leftCtrlPressed)
			{
				editor.swapChannelFlag = true;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("SWAP (1/2/3/4) ?", NO_CARRY);
			}
			else if (keyb.leftAltPressed)
			{
				toggleTuningTone();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_U:
		{
			if (keyb.leftCtrlPressed)
				undoLastChange();
			else
				handleEditKeys(scancode, EDIT_NORMAL);
		}
		break;

		case SDL_SCANCODE_V:
		{
			if (keyb.leftAmigaPressed)
			{
				pattOctaDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (ui.samplerScreenShown)
				{
					samplerSamPaste();
				}
				else
				{
					ui.askScreenShown = true;
					ui.askScreenType = ASK_FILTER_ALL_SAMPLES;

					pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
					setStatusMessage("FILTER ALL SAMPLS", NO_CARRY);
					renderAskDialog();
				}
			}
			else if (keyb.leftAltPressed)
			{
				editor.muted[3] ^= 1; // toggle channel 4
				renderMuteButtons();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_W:
		{
			if (keyb.leftAmigaPressed)
			{
				pattNoteDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				// Polyphonize Block
				if (!editor.blockBufferFlag)
				{
					displayErrorMsg("BUFFER IS EMPTY !");
					return;
				}

				saveUndo();

				i = editor.buffFromPos;
				j = song->currRow;
				patt = song->patterns[song->currPattern];
				while (true)
				{
					noteDst = &patt[(j * AMIGA_VOICES) + cursor.channel];
					if (editor.blockBuffer[i].period == 0 && editor.blockBuffer[i].sample == 0)
					{
						noteDst->command = editor.blockBuffer[i].command;
						noteDst->param = editor.blockBuffer[i].param;
					}
					else
					{
						*noteDst = editor.blockBuffer[i];
					}

					if (i == editor.buffToPos || i == 63 || j == 63)
						break;

					i++;
					j++;
					gotoNextMulti();
				}

				if (!keyb.shiftPressed)
				{
					song->currRow += (editor.buffToPos-editor.buffFromPos) + 1;
					if (song->currRow > 63)
						song->currRow = 0;
				}

				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_X:
		{
			if (keyb.leftAmigaPressed)
			{
				pattOctaDown(TRANSPOSE_ALL);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (ui.samplerScreenShown)
				{
					samplerSamDelete(SAMPLE_CUT);
					return;
				}

				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				editor.blockMarkFlag = false;
				saveUndo();
				editor.blockBufferFlag = true;

				for (i = 0; i < MOD_ROWS; i++)
					editor.blockBuffer[i] = song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];

				if (editor.blockFromPos > editor.blockToPos)
				{
					editor.buffFromPos = editor.blockToPos;
					editor.buffToPos = editor.blockFromPos;
				}
				else
				{
					editor.buffFromPos = editor.blockFromPos;
					editor.buffToPos = editor.blockToPos;
				}

				for (i = editor.buffFromPos; i <= editor.buffToPos; i++)
				{
					noteDst = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];
					noteDst->period = 0;
					noteDst->sample = 0;
					noteDst->command = 0;
					noteDst->param = 0;
				}

				statusAllRight();
				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else
			{
				if (keyb.leftAltPressed)
				{
					editor.muted[1] ^= 1; // toggle channel 2
					renderMuteButtons();
				}
				else
				{
					handleEditKeys(scancode, EDIT_NORMAL);
				}
			}
		}
		break;

		case SDL_SCANCODE_Y:
		{
			if (keyb.leftCtrlPressed)
			{
				if (!editor.blockMarkFlag)
				{
					displayErrorMsg("NO BLOCK MARKED !");
					return;
				}

				editor.blockMarkFlag = false;

				saveUndo();

				if (editor.blockFromPos >= editor.blockToPos)
				{
					blockFrom = editor.blockToPos;
					blockTo = editor.blockFromPos;
				}
				else
				{
					blockFrom = editor.blockFromPos;
					blockTo = editor.blockToPos;
				}

				while (blockFrom < blockTo)
				{
					noteDst = &song->patterns[song->currPattern][(blockFrom * AMIGA_VOICES) + cursor.channel];
					noteSrc = &song->patterns[song->currPattern][(blockTo * AMIGA_VOICES) + cursor.channel];

					noteTmp = *noteDst;
					*noteDst = *noteSrc;
					*noteSrc = noteTmp;

					blockFrom += 1;
					blockTo -= 1;
				}

				statusAllRight();
				updateWindowTitle(MOD_IS_MODIFIED);
				ui.updatePatternData = true;
			}
			else if (keyb.leftAltPressed)
			{
				ui.askScreenShown = true;
				ui.askScreenType = ASK_SAVE_ALL_SAMPLES;
				pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
				setStatusMessage("SAVE ALL SAMPLES?", NO_CARRY);
				renderAskDialog();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		case SDL_SCANCODE_Z:
		{
			if (keyb.leftAmigaPressed)
			{
				trackOctaDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
			}
			else if (keyb.leftCtrlPressed)
			{
				if (ui.samplerScreenShown)
				{
					ui.askScreenShown = true;
					ui.askScreenType = ASK_RESTORE_SAMPLE;

					pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
					setStatusMessage("RESTORE SAMPLE ?", NO_CARRY);
					renderAskDialog();
				}
				else
				{
					modSetTempo(125, true);
					modSetSpeed(6);

					for (i = 0; i < AMIGA_VOICES; i++)
					{
						ch = &song->channels[i];
						ch->n_wavecontrol = 0;
						ch->n_glissfunk = 0;
						ch->n_finetune = 0;
						ch->n_loopcount = 0;
					}

					displayMsg("EFX RESTORED !");
				}
			}
			else if (keyb.leftAltPressed)
			{
				editor.muted[0] ^= 1; // toggle channel 1
				renderMuteButtons();
			}
			else
			{
				handleEditKeys(scancode, EDIT_NORMAL);
			}
		}
		break;

		default:
			handleEditKeys(scancode, EDIT_NORMAL);
		break;
	}
}

void movePatCurPrevCh(void)
{
	int8_t pos = ((cursor.pos + 5) / 6) - 1;

	cursor.pos = (pos < 0) ? (3 * 6) : (pos * 6);
	cursor.mode = CURSOR_NOTE;

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	updateCursorPos();
}

void movePatCurNextCh(void)
{
	int8_t pos = (cursor.pos / 6) + 1;

	cursor.pos = (pos == 4) ? 0 : (pos * 6);
	cursor.mode = CURSOR_NOTE;

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	updateCursorPos();
}

void movePatCurRight(void)
{
	cursor.pos = (cursor.pos == 23) ? 0 : (cursor.pos + 1);

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	cursor.mode = cursor.pos % 6;
	updateCursorPos();
}

void movePatCurLeft(void)
{
	cursor.pos = (cursor.pos == 0) ? 23 : (cursor.pos - 1);

	     if (cursor.pos <  6) cursor.channel = 0;
	else if (cursor.pos < 12) cursor.channel = 1;
	else if (cursor.pos < 18) cursor.channel = 2;
	else if (cursor.pos < 24) cursor.channel = 3;

	cursor.mode = cursor.pos % 6;
	updateCursorPos();
}

void handleKeyRepeat(SDL_Scancode scancode)
{
	uint8_t repeatNum;

	if (!keyb.repeatKey || (ui.clearScreenShown || ui.askScreenShown))
	{
		keyb.repeatFrac = 0;
		keyb.repeatCounter = 0;
		return;
	}

	if (keyb.delayKey && keyb.delayCounter < KEYB_REPEAT_DELAY)
	{
		keyb.delayCounter++;
		return;
	}

	switch (scancode) // only some buttons have repeat
	{
		case SDL_SCANCODE_PAGEUP:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;

				if (ui.posEdScreenShown)
				{
					if (song->currOrder-(POSED_LIST_SIZE-1) > 0)
						modSetPos(song->currOrder-(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(0, DONT_SET_ROW);
				}
				else if (ui.diskOpScreenShown)
				{
					if (ui.diskOpScreenShown)
					{
						diskop.scrollOffset -= DISKOP_LINES-1;
						if (diskop.scrollOffset < 0)
							diskop.scrollOffset = 0;

						ui.updateDiskOpFileList = true;
					}
				}
				else if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					if (song->currRow == 63)
						modSetPos(DONT_SET_ORDER, song->currRow - 15);
					else if (song->currRow == 15)
						modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
					else
						modSetPos(DONT_SET_ORDER, song->currRow - 16);
				}
			}
		}
		break;

		case SDL_SCANCODE_PAGEDOWN:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;

				if (ui.posEdScreenShown)
				{
					if (song->currOrder+(POSED_LIST_SIZE-1) <= song->header.numOrders-1)
						modSetPos(song->currOrder+(POSED_LIST_SIZE-1), DONT_SET_ROW);
					else
						modSetPos(song->header.numOrders - 1, DONT_SET_ROW);
				}
				else if (ui.diskOpScreenShown)
				{
					if (diskop.numEntries > DISKOP_LINES)
					{
						diskop.scrollOffset += DISKOP_LINES-1;
						if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
							diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

						ui.updateDiskOpFileList = true;
					}
				}
				else if (editor.currMode == MODE_IDLE || editor.currMode == MODE_EDIT)
				{
					modSetPos(DONT_SET_ORDER, song->currRow + 16);
				}
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			if (ui.editTextFlag)
			{
				if (keyb.delayCounter >= KEYB_REPEAT_DELAY)
				{
					if (keyb.repeatCounter >= 3)
					{
						keyb.repeatCounter = 0;
						textCharPrevious();
					}
				}
				else
				{
					keyb.delayCounter++;
				}
			}
			else
			{
				if (keyb.leftCtrlPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						sampleDownButton();
					}
				}
				else if (keyb.shiftPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (song->currOrder > 0)
							modSetPos(song->currOrder - 1, DONT_SET_ROW);
					}
				}
				else if (keyb.leftAltPressed)
				{
					if (keyb.repeatCounter >= 4)
					{
						keyb.repeatCounter = 0;
						decPatt();
					}
				}
				else
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (!keyb.shiftPressed && !keyb.leftAltPressed && !keyb.leftCtrlPressed)
							movePatCurLeft();
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_RIGHT:
		{
			if (ui.editTextFlag)
			{
				if (keyb.delayCounter >= KEYB_REPEAT_DELAY)
				{
					if (keyb.repeatCounter >= 3)
					{
						keyb.repeatCounter = 0;
						textCharNext();
					}
				}
				else
				{
					keyb.delayCounter++;
				}
			}
			else
			{
				if (keyb.leftCtrlPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						sampleUpButton();
					}
				}
				else if (keyb.shiftPressed)
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (song->currOrder < 126)
							modSetPos(song->currOrder + 1, DONT_SET_ROW);
					}
				}
				else if (keyb.leftAltPressed)
				{
					if (keyb.repeatCounter >= 4)
					{
						keyb.repeatCounter = 0;
						incPatt();
					}
				}
				else
				{
					if (keyb.repeatCounter >= 6)
					{
						keyb.repeatCounter = 0;
						if (!keyb.shiftPressed && !keyb.leftAltPressed && !keyb.leftCtrlPressed)
							movePatCurRight();
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_UP:
		{
			if (ui.diskOpScreenShown)
			{
				if (keyb.repeatCounter >= 1)
				{
					keyb.repeatCounter = 0;

					diskop.scrollOffset--;
					if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
						diskop.scrollOffset -= 3;

					if (diskop.scrollOffset < 0)
						diskop.scrollOffset = 0;

					ui.updateDiskOpFileList = true;
				}
			}
			else if (ui.posEdScreenShown)
			{
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;
					if (song->currOrder > 0)
					{
						modSetPos(song->currOrder - 1, DONT_SET_ROW);
						ui.updatePosEd = true;
					}
				}
			}
			else if (!ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					repeatNum = 6;
					if (keyb.leftAltPressed)
						repeatNum = 1;
					else if (keyb.shiftPressed)
						repeatNum = 3;

					if (keyb.repeatCounter >= repeatNum)
					{
						keyb.repeatCounter = 0;
						modSetPos(DONT_SET_ORDER, (song->currRow - 1) & 0x3F);
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_DOWN:
		{
			if (ui.diskOpScreenShown)
			{
				if (keyb.repeatCounter >= 1)
				{
					keyb.repeatCounter = 0;

					if (diskop.numEntries > DISKOP_LINES)
					{
						diskop.scrollOffset++;
						if (mouse.rightButtonPressed) // PT quirk: right mouse button speeds up scrolling even on keyb UP/DOWN
							diskop.scrollOffset += 3;

						if (diskop.scrollOffset > diskop.numEntries-DISKOP_LINES)
							diskop.scrollOffset = diskop.numEntries-DISKOP_LINES;

						ui.updateDiskOpFileList = true;
					}
				}
			}
			else if (ui.posEdScreenShown)
			{
				if (keyb.repeatCounter >= 3)
				{
					keyb.repeatCounter = 0;

					if (song->currOrder != song->header.numOrders-1)
					{
						if (++song->currOrder > song->header.numOrders-1)
							song->currOrder = song->header.numOrders-1;

						modSetPos(song->currOrder, DONT_SET_ROW);
						ui.updatePosEd = true;
					}
				}
			}
			else if (!ui.samplerScreenShown)
			{
				if (editor.currMode != MODE_PLAY && editor.currMode != MODE_RECORD)
				{
					repeatNum = 6;
					if (keyb.leftAltPressed)
						repeatNum = 1;
					else if (keyb.shiftPressed)
						repeatNum = 3;

					if (keyb.repeatCounter >= repeatNum)
					{
						keyb.repeatCounter = 0;
						modSetPos(DONT_SET_ORDER, (song->currRow + 1) & 0x3F);
					}
				}
			}
		}
		break;

		case SDL_SCANCODE_BACKSPACE:
		{
			if (ui.editTextFlag)
			{
				// only repeat backspace while editing texts

				if (keyb.delayCounter >= KEYB_REPEAT_DELAY)
				{
					if (keyb.repeatCounter >= 3)
					{
						keyb.repeatCounter = 0;
						keyDownHandler(scancode, 0);
					}
				}
				else
				{
					keyb.delayCounter++;
				}
			}
		}
		break;

		case SDL_SCANCODE_KP_ENTER:
		case SDL_SCANCODE_RETURN:
			break; // do NOT repeat enter!

		default:
		{
			if (keyb.repeatCounter >= 3)
			{
				keyb.repeatCounter = 0;
				keyDownHandler(scancode, 0);
			}
		}
		break;
	}

	keyb.repeatFrac += keyb.repeatDelta; // 32.32 fixed-point counter
	if (keyb.repeatFrac > 0xFFFFFFFF)
	{
		keyb.repeatFrac &= 0xFFFFFFFF;
		keyb.repeatCounter++;
	}
}

static void swapChannel(uint8_t srcCh, uint8_t dstCh)
{
	if (srcCh == dstCh)
		return;

	for (int32_t i = 0; i < MOD_ROWS; i++)
	{
		note_t *noteSrc = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + dstCh];
		note_t noteTmp = song->patterns[song->currPattern][(i * AMIGA_VOICES) + srcCh];

		song->patterns[song->currPattern][(i * AMIGA_VOICES) + srcCh] = *noteSrc;
		*noteSrc = noteTmp;
	}

	updateWindowTitle(MOD_IS_MODIFIED);
	ui.updatePatternData = true;
}

bool handleGeneralModes(SDL_Keycode keycode, SDL_Scancode scancode)
{
	int8_t rawKey;

	// SAMPLER SCREEN (volume box)
	if (ui.samplerVolBoxShown && !ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		ui.samplerVolBoxShown = false;
		removeSamplerVolBox();
		return false;
	}

	// SAMPLER SCREEN (filters box)
	if (ui.samplerFiltersBoxShown && !ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		ui.samplerFiltersBoxShown = false;
		removeSamplerFiltersBox();
		return false;
	}

	// SAMPLER SCREEN (sampling box)
	if (ui.samplingBoxShown)
	{
		if (audio.isSampling)
		{
			stopSampling();
			return false;
		}


		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		if (ui.changingSamplingNote)
		{
			if (scancode == SDL_SCANCODE_ESCAPE)
			{
				ui.changingSamplingNote = false;
				setPrevStatusMessage();
				pointerSetPreviousMode();
			}

			rawKey = keyToNote(scancode);
			if (rawKey >= 0)
			{
				ui.changingSamplingNote = false;

				setSamplingNote(rawKey);

				setPrevStatusMessage();
				pointerSetPreviousMode();
			}

			return false;
		}
		else
		{
			if (keyb.leftCtrlPressed)
			{
				if (scancode == SDL_SCANCODE_LEFT)
					samplingSampleNumDown();
				else if (scancode == SDL_SCANCODE_RIGHT)
					samplingSampleNumUp();
			}
			else
			{
				if (scancode == SDL_SCANCODE_SPACE)
					turnOffVoices();
				else
					handleSampleJamming(scancode);
			}
		}

		if (!ui.editTextFlag && scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.samplingBoxShown = false;
			removeSamplingBox();
		}

		return false;
	}

	// EDIT OP. SCREEN #3
	if (editor.mixFlag && scancode == SDL_SCANCODE_ESCAPE)
	{
		exitGetTextLine(EDIT_TEXT_UPDATE);
		editor.mixFlag = false;
		ui.updateMixText = true;
		return false;
	}

	// EDIT OP. SCREEN #4
	if (ui.changingChordNote)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.changingChordNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			if (ui.changingChordNote == 1)
			{
				editor.note1 = rawKey;
				ui.updateChordNote1Text = true;
			}
			else if (ui.changingChordNote == 2)
			{
				editor.note2 = rawKey;
				ui.updateChordNote2Text = true;
			}
			else if (ui.changingChordNote == 3)
			{
				editor.note3 = rawKey;
				ui.updateChordNote3Text = true;
			}
			else if (ui.changingChordNote == 4)
			{
				editor.note4 = rawKey;
				ui.updateChordNote4Text = true;
			}

			ui.changingChordNote = false;
			recalcChordLength();

			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// CHANGE DRUMPAD NOTE
	if (ui.changingDrumPadNote)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.changingDrumPadNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			pNoteTable[editor.currSample] = rawKey;
			ui.changingDrumPadNote = false;
			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// SAMPLER SCREEN
	if (ui.changingSmpResample)
	{
		if (scancode == SDL_SCANCODE_ESCAPE)
		{
			ui.changingSmpResample = false;
			ui.updateResampleNote = true;
			setPrevStatusMessage();
			pointerSetPreviousMode();
			return false;
		}

		if (scancode == SDL_SCANCODE_F1)
			editor.keyOctave = OCTAVE_LOW;
		else if (scancode == SDL_SCANCODE_F2)
			editor.keyOctave = OCTAVE_HIGH;

		rawKey = keyToNote(scancode);
		if (rawKey >= 0)
		{
			editor.resampleNote = rawKey;
			ui.changingSmpResample = false;
			ui.updateResampleNote = true;
			setPrevStatusMessage();
			pointerSetPreviousMode();
		}

		return false;
	}

	// DISK OP. SCREEN
	if (diskop.isFilling)
	{
		if (ui.askScreenShown && ui.askScreenType == ASK_QUIT)
		{
			if (keycode == SDLK_y || keycode == SDLK_RETURN)
			{
				ui.askScreenShown = false;
				ui.answerNo = false;
				ui.answerYes = true;
				handleAskYes();
			}
			else if (keycode == SDLK_n || keycode == SDLK_ESCAPE)
			{
				ui.askScreenShown = false;
				ui.answerNo = true;
				ui.answerYes = false;
				handleAskNo();
			}
		}

		return false;
	}

	// if MOD2WAV is ongoing, only react to ESC and Y/N on exit ask dialog
	if (editor.isWAVRendering)
	{
		if (ui.askScreenShown && ui.askScreenType == ASK_QUIT)
		{
			if (keycode == SDLK_y)
			{
				editor.isWAVRendering = false;
				SDL_WaitThread(editor.mod2WavThread, NULL);

				ui.askScreenShown = false;
				ui.answerNo = false;
				ui.answerYes = true;

				handleAskYes();
			}
			else if (keycode == SDLK_n)
			{
				ui.askScreenShown = false;
				ui.answerNo = true;
				ui.answerYes = false;

				handleAskNo();

				pointerSetMode(POINTER_MODE_MSG2, NO_CARRY);
				setStatusMessage("RENDERING MOD...", NO_CARRY);
			}
		}
		else if (scancode == SDL_SCANCODE_ESCAPE)
		{
			editor.abortMod2Wav = true;
		}

		return false;
	}

	// SWAP CHANNEL (CTRL+T)
	if (editor.swapChannelFlag)
	{
		switch (scancode)
		{
			case SDL_SCANCODE_ESCAPE:
			{
				editor.swapChannelFlag = false;
				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_1:
			{
				swapChannel(0, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_2:
			{
				swapChannel(1, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_3:
			{
				swapChannel(2, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDL_SCANCODE_4:
			{
				swapChannel(3, cursor.channel);
				editor.swapChannelFlag = false;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			default: break;
		}

		return false;
	}

	// YES/NO ASK DIALOG
	if (ui.askScreenShown)
	{
		if (ui.pat2SmpDialogShown)
		{
			// PAT2SMP specific ask dialog
			switch (keycode)
			{
				case SDLK_KP_ENTER:
				case SDLK_RETURN:
				case SDLK_h:
				{
					ui.askScreenShown = false;
					ui.answerNo = true;
					ui.answerYes = false;
					editor.pat2SmpHQ = true;
					handleAskYes();
				}
				break;

				case SDLK_l:
				{
					ui.askScreenShown = false;
					ui.answerNo = false;
					ui.answerYes = true;
					editor.pat2SmpHQ = false;
					handleAskYes();
					// pointer/status is updated by the 'yes handler'
				}
				break;

				case SDLK_ESCAPE:
				case SDLK_a:
				case SDLK_n:
				{
					ui.askScreenShown = false;
					ui.answerNo = true;
					ui.answerYes = false;
					handleAskNo();
				}
				break;

				default: break;
			}
		}
		else
		{
			// normal yes/no dialog
			switch (keycode)
			{
				case SDLK_ESCAPE:
				case SDLK_n:
				{
					ui.askScreenShown = false;
					ui.answerNo = true;
					ui.answerYes = false;
					handleAskNo();
				}
				break;

				case SDLK_KP_ENTER:
				case SDLK_RETURN:
				case SDLK_y:
				{
					ui.askScreenShown = false;
					ui.answerNo = false;
					ui.answerYes = true;
					handleAskYes();
					// pointer/status is updated by the 'yes handler'
				}
				break;

				default: break;
			}
		}

		return false;
	}

	// CLEAR SCREEN DIALOG
	if (ui.clearScreenShown)
	{
		switch (keycode)
		{
			case SDLK_s:
			{
				ui.clearScreenShown = false;
				removeClearScreen();

				modStop();
				clearSamples();

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDLK_o:
			{
				ui.clearScreenShown = false;
				removeClearScreen();

				modStop();
				clearSong();

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDLK_a:
			{
				ui.clearScreenShown = false;
				removeClearScreen();

				modStop();
				clearAll();

				editor.playMode = PLAY_MODE_NORMAL;
				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			case SDLK_c:
			case SDLK_ESCAPE:
			{
				ui.clearScreenShown = false;
				removeClearScreen();

				editor.currMode = MODE_IDLE;

				pointerSetPreviousMode();
				setPrevStatusMessage();
			}
			break;

			default: break;
		}

		return false;
	}

	return true;
}

void handleTextEditInputChar(char textChar)
{
	char *readTmp;
	int8_t readTmpPrev;
	uint8_t digit1, digit2, digit3, digit4;
	uint32_t i, number;

	// we only want certain keys
	if (textChar < ' ' || textChar > '~')
		return;

	// A..Z -> a..z
	if (textChar >= 'A' && textChar <= 'Z')
		textChar = (char)tolower(textChar);

	if (ui.editTextType == TEXT_EDIT_STRING)
	{
		if (ui.editPos < ui.textEndPtr)
		{
			if (!editor.mixFlag)
			{
				readTmp = ui.textEndPtr;
				while (readTmp > ui.editPos)
				{
					readTmpPrev = *--readTmp;
					*(readTmp + 1) = readTmpPrev;
				}

				*ui.textEndPtr = '\0';
				*ui.editPos++ = textChar;

				textMarkerMoveRight();
			}
			else if ((textChar >= '0' && textChar <= '9') || (textChar >= 'a' && textChar <= 'f'))
			{
				if (ui.dstPos == 14) // hack for sample mix text
				{
					*ui.editPos = textChar;
				}
				else
				{
					*ui.editPos++ = textChar;
					textMarkerMoveRight();

					if (ui.dstPos == 9) // hack for sample mix text
					{
						for (i = 0; i < 4; i++)
						{
							ui.editPos++;
							textMarkerMoveRight();
						}
					}
					else if (ui.dstPos == 6) // hack for sample mix text
					{
						ui.editPos++;
						textMarkerMoveRight();
					}
				}
			}
		}
	}
	else
	{
		if (ui.editTextType == TEXT_EDIT_DECIMAL)
		{
			if (textChar >= '0' && textChar <= '9')
			{
				textChar -= '0';

				if (ui.numLen == 4)
				{
					number = *ui.numPtr16;
					digit4 = number % 10; number /= 10;
					digit3 = number % 10; number /= 10;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (ui.dstPos == 0) *ui.numPtr16 = (textChar * 1000) + (digit2 * 100) + (digit3 * 10) + digit4;
					else if (ui.dstPos == 1) *ui.numPtr16 = (digit1 * 1000) + (textChar * 100) + (digit3 * 10) + digit4;
					else if (ui.dstPos == 2) *ui.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (textChar * 10) + digit4;
					else if (ui.dstPos == 3) *ui.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (digit3 * 10) + textChar;
				}
				else if (ui.numLen == 3)
				{
					number = *ui.numPtr16;
					digit3 = number % 10; number /= 10;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (ui.dstPos == 0) *ui.numPtr16 = (textChar * 100) + (digit2 * 10) + digit3;
					else if (ui.dstPos == 1) *ui.numPtr16 = (digit1 * 100) + (textChar * 10) + digit3;
					else if (ui.dstPos == 2) *ui.numPtr16 = (digit1 * 100) + (digit2 * 10) + textChar;
				}
				else if (ui.numLen == 2)
				{
					number = *ui.numPtr16;
					digit2 = number % 10; number /= 10;
					digit1 = (uint8_t)number;

					     if (ui.dstPos == 0) *ui.numPtr16 = (textChar * 10) + digit2;
					else if (ui.dstPos == 1) *ui.numPtr16 = (digit1 * 10) + textChar;
				}

				textMarkerMoveRight();
				if (ui.dstPos >= ui.numLen)
					exitGetTextLine(EDIT_TEXT_UPDATE);
			}
		}
		else
		{
			if ((textChar >= '0' && textChar <= '9') || (textChar >= 'a' && textChar <= 'f'))
			{
				if (textChar <= '9')
					textChar -= '0';
				else if (textChar <= 'f')
					textChar -= 'a'-10;

				if (ui.numBits == 17)
				{
					*ui.numPtr32 &= ~(0xF0000 >> (ui.dstPos << 2));
					*ui.numPtr32 |= textChar << (16 - (ui.dstPos << 2));
				}
				else if (ui.numBits == 16)
				{
					if (ui.force32BitNumPtr)
					{
						*ui.numPtr32 &= ~(0xF000 >> (ui.dstPos << 2));
						*ui.numPtr32 |= textChar << (12 - (ui.dstPos << 2));
					}
					else
					{
						*ui.numPtr16 &= ~(0xF000 >> (ui.dstPos << 2));
						*ui.numPtr16 |= textChar << (12 - (ui.dstPos << 2));
					}
				}
				else if (ui.numBits == 8)
				{
					*ui.numPtr8 &= ~(0xF0 >> (ui.dstPos << 2));
					*ui.numPtr8 |= textChar << (4 - (ui.dstPos << 2));
				}

				textMarkerMoveRight();
				if (ui.dstPos >= ui.numLen)
					exitGetTextLine(EDIT_TEXT_UPDATE);
			}
		}
	}

	updateTextObject(ui.editObject);

	if (!keyb.repeatKey)
		keyb.delayCounter = 0;

	keyb.repeatKey = true;
	keyb.delayKey = true;
}

bool handleTextEditMode(SDL_Scancode scancode)
{
	char *readTmp;
	int8_t readTmpNext;
	int16_t i, j;
	note_t *noteSrc, *noteDst;

	switch (scancode)
	{
		case SDL_SCANCODE_ESCAPE:
		{
			editor.blockMarkFlag = false;
			if (ui.editTextFlag)
			{
				exitGetTextLine(EDIT_TEXT_NO_UPDATE);
				return false;
			}
		}
		break;

		case SDL_SCANCODE_HOME:
		{
			if (ui.editTextFlag && !editor.mixFlag)
			{
				while (ui.editPos > ui.showTextPtr)
					textCharPrevious();
			}
		}
		break;

		case SDL_SCANCODE_END:
		{
			if (ui.editTextFlag && !editor.mixFlag)
			{
				if (ui.editTextType != TEXT_EDIT_STRING)
					break;

				while (!ui.dstOffsetEnd)
					textCharNext();
			}
		}
		break;

		case SDL_SCANCODE_LEFT:
		{
			if (ui.editTextFlag)
			{
				textCharPrevious();
				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				keyb.delayKey = false;
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_RIGHT:
		{
			if (ui.editTextFlag)
			{
				textCharNext();
				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				keyb.delayKey = false;
				keyb.repeatKey = true;
			}
		}
		break;

		case SDL_SCANCODE_DELETE:
		{
			if (ui.editTextFlag)
			{
				if (editor.mixFlag || ui.editTextType != TEXT_EDIT_STRING)
					break;

				readTmp = ui.editPos;
				while (readTmp < ui.textEndPtr)
				{
					readTmpNext = *(readTmp + 1);
					*readTmp++ = readTmpNext;
				}

				// kludge to prevent cloning last character if the song/sample name has one character too much
				if (ui.editObject == PTB_SONGNAME || ui.editObject == PTB_SAMPLENAME)
					 *ui.textEndPtr = '\0';

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = true;

				updateTextObject(ui.editObject);
			}
		}
		break;

		case SDL_SCANCODE_BACKSPACE:
		{
			if (ui.editTextFlag)
			{
				if (editor.mixFlag || ui.editTextType != TEXT_EDIT_STRING)
					break;

				if (ui.editPos > ui.dstPtr)
				{
					ui.editPos--;

					readTmp = ui.editPos;
					while (readTmp < ui.textEndPtr)
					{
						readTmpNext = *(readTmp + 1);
						*readTmp++ = readTmpNext;
					}

					// kludge to prevent cloning last character if the song/sample name has one character too much
					if (ui.editObject == PTB_SONGNAME || ui.editObject == PTB_SAMPLENAME)
						*ui.textEndPtr = '\0';

					textMarkerMoveLeft();
					updateTextObject(ui.editObject);
				}

				if (!keyb.repeatKey)
					keyb.delayCounter = 0;

				keyb.repeatKey = true;
				keyb.delayKey = false;
			}
			else
			{
				if (ui.diskOpScreenShown)
				{
#ifdef _WIN32
					diskOpSetPath(L"..", DISKOP_CACHE);
#else
					diskOpSetPath("..", DISKOP_CACHE);
#endif
				}
				else if (keyb.shiftPressed || keyb.leftAltPressed || keyb.leftCtrlPressed)
				{
					saveUndo();
					if (keyb.leftAltPressed && !keyb.leftCtrlPressed)
					{
						if (song->currRow > 0)
						{
							for (i = 0; i < AMIGA_VOICES; i++)
							{
								for (j = (song->currRow - 1); j < MOD_ROWS; j++)
								{
									noteSrc = &song->patterns[song->currPattern][((j + 1) * AMIGA_VOICES) + i];
									song->patterns[song->currPattern][(j * AMIGA_VOICES) + i] = *noteSrc;
								}

								// clear newly made row on very bottom
								noteDst = &song->patterns[song->currPattern][(63 * AMIGA_VOICES) + i];
								noteDst->period = 0;
								noteDst->sample = 0;
								noteDst->command = 0;
								noteDst->param = 0;
							}

							song->currRow--;
							ui.updatePatternData = true;
						}
					}
					else
					{
						if (song->currRow > 0)
						{
							for (i = song->currRow-1; i < MOD_ROWS-1; i++)
							{
								noteSrc = &song->patterns[song->currPattern][((i + 1) * AMIGA_VOICES) + cursor.channel];
								noteDst = &song->patterns[song->currPattern][(i * AMIGA_VOICES) + cursor.channel];

								if (keyb.leftCtrlPressed)
								{
									noteDst->command = noteSrc->command;
									noteDst->param = noteSrc->param;
								}
								else
								{
									*noteDst = *noteSrc;
								}
							}

							// clear newly made row on very bottom
							noteDst = &song->patterns[song->currPattern][(63 * AMIGA_VOICES) + cursor.channel];
							noteDst->period = 0;
							noteDst->sample = 0;
							noteDst->command = 0;
							noteDst->param = 0;

							song->currRow--;
							ui.updatePatternData = true;
						}
					}
				}
				else
				{
					editor.stepPlayEnabled = true;
					editor.stepPlayBackwards = true;

					editor.stepPlayLastMode = editor.currMode;

					if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
						doStopIt(false);
					else
						doStopIt(true);

					playPattern((song->currRow - 1) & 0x3F);

					if (config.keepEditModeAfterStepPlay && editor.stepPlayLastMode == MODE_EDIT)
					{
						pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
						editor.playMode = PLAY_MODE_NORMAL;
						editor.currMode = MODE_EDIT;
					}
				}
			}
		}
		break;

		default: break;
	}

	if (ui.editTextFlag)
	{
		if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER)
		{
			// dirty hack
			if (ui.editObject == PTB_SAMPLES)
				ui.tmpDisp8++;

			exitGetTextLine(EDIT_TEXT_UPDATE);

			if (editor.mixFlag)
			{
				editor.mixFlag = false;
				ui.updateMixText = true;
				doMix();
			}
		}

		return false; // don't continue further key handling
	}

	return true; // continue further key handling (we're not editing text)
}
