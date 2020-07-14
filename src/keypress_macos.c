#include "keypress.h"
#include "microsleep.h"

#include <ctype.h> /* For isupper() */

#include <ApplicationServices/ApplicationServices.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#import <IOKit/hidsystem/ev_keymap.h>

static io_connect_t _getAuxiliaryKeyDriver(void)
{
	static mach_port_t sEventDrvrRef = 0;
	mach_port_t masterPort, service, iter;
	kern_return_t kr;

	if (!sEventDrvrRef)
	{
		kr = IOMasterPort(bootstrap_port, &masterPort);
		assert(KERN_SUCCESS == kr);
		kr = IOServiceGetMatchingServices(masterPort, IOServiceMatching(kIOHIDSystemClass), &iter);
		assert(KERN_SUCCESS == kr);
		service = IOIteratorNext(iter);
		assert(service);
		kr = IOServiceOpen(service, mach_task_self(), kIOHIDParamConnectType, &sEventDrvrRef);
		assert(KERN_SUCCESS == kr);
		IOObjectRelease(service);
		IOObjectRelease(iter);
	}
	return sEventDrvrRef;
}

void toggleKeyCode(MMKeyCode code, const bool down, MMKeyFlags flags)
{
	/* The media keys all have 1000 added to them to help us detect them. */
	if (code >= 1000)
	{
		code = code - 1000; /* Get the real keycode. */
		NXEventData event;
		kern_return_t kr;
		IOGPoint loc = {0, 0};
		UInt32 evtInfo = code << 16 | (down ? NX_KEYDOWN : NX_KEYUP) << 8;
		bzero(&event, sizeof(NXEventData));
		event.compound.subType = NX_SUBTYPE_AUX_CONTROL_BUTTONS;
		event.compound.misc.L[0] = evtInfo;
		kr = IOHIDPostEvent(_getAuxiliaryKeyDriver(), NX_SYSDEFINED, loc, &event, kNXEventDataVersion, 0, FALSE);
		assert(KERN_SUCCESS == kr);
	}
	else
	{
		CGEventRef keyEvent = CGEventCreateKeyboardEvent(NULL,
														 (CGKeyCode)code, down);
		assert(keyEvent != NULL);

		CGEventSetType(keyEvent, down ? kCGEventKeyDown : kCGEventKeyUp);
		CGEventSetFlags(keyEvent, flags);
		CGEventPost(kCGSessionEventTap, keyEvent);
		CFRelease(keyEvent);
	}
}

void tapKeyCode(MMKeyCode code, MMKeyFlags flags)
{
	toggleKeyCode(code, true, flags);
	toggleKeyCode(code, false, flags);
}

void toggleKey(char c, const bool down, MMKeyFlags flags)
{
	MMKeyCode keyCode = keyCodeForChar(c);

	if (isupper(c) && !(flags & MOD_SHIFT))
	{
		flags |= MOD_SHIFT; /* Not sure if this is safe for all layouts. */
	}

	toggleKeyCode(keyCode, down, flags);
}

void tapKey(char c, MMKeyFlags flags)
{
	toggleKey(c, true, flags);
	toggleKey(c, false, flags);
}

void toggleUnicodeKey(unsigned long ch, const bool down)
{
	/* This function relies on the convenient
	 * CGEventKeyboardSetUnicodeString(), which allows us to not have to
	 * convert characters to a keycode, but does not support adding modifier
	 * flags. It is therefore only used in typeString() and typeStringDelayed()
	 * -- if you need modifier keys, use the above functions instead. */
	CGEventRef keyEvent = CGEventCreateKeyboardEvent(NULL, 0, down);
	if (keyEvent == NULL)
	{
		fputs("Could not create keyboard event.\n", stderr);
		return;
	}

	if (ch > 0xFFFF)
	{
		// encode to utf-16 if necessary
		unsigned short surrogates[] = {
			0xD800 + ((ch - 0x10000) >> 10),
			0xDC00 + (ch & 0x3FF)};

		CGEventKeyboardSetUnicodeString(keyEvent, 2, &surrogates);
	}
	else
	{
		CGEventKeyboardSetUnicodeString(keyEvent, 1, &ch);
	}

	CGEventPost(kCGSessionEventTap, keyEvent);
	CFRelease(keyEvent);
}

static void tapUnicodeKey(char c)
{
	toggleUnicodeKey(c, true);
	toggleUnicodeKey(c, false);
}

void typeString(const char *str)
{
	unsigned short c;
	unsigned short c1;
	unsigned short c2;
	unsigned short c3;
	unsigned long n;

	while (*str != '\0')
	{
		c = *str++;

		// warning, the following utf8 decoder
		// doesn't perform validation
		if (c <= 0x7F)
		{
			// 0xxxxxxx one byte
			n = c;
		}
		else if ((c & 0xE0) == 0xC0)
		{
			// 110xxxxx two bytes
			c1 = (*str++) & 0x3F;
			n = ((c & 0x1F) << 6) | c1;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			// 1110xxxx three bytes
			c1 = (*str++) & 0x3F;
			c2 = (*str++) & 0x3F;
			n = ((c & 0x0F) << 12) | (c1 << 6) | c2;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			// 11110xxx four bytes
			c1 = (*str++) & 0x3F;
			c2 = (*str++) & 0x3F;
			c3 = (*str++) & 0x3F;
			n = ((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3;
		}

		tapUnicodeKey(n);
	}
}

void typeStringDelayed(const char *str, const unsigned cpm)
{
	/* Characters per second */
	const double cps = (double)cpm / 60.0;

	/* Average milli-seconds per character */
	const double mspc = (cps == 0.0) ? 0.0 : 1000.0 / cps;

	while (*str != '\0')
	{
		tapUnicodeKey(*str++);
		if (mspc > 0)
		{
			microsleep(mspc);
		}
	}
}
