/*
 * Copyright 2001-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, burton666@libero.it
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Marc Flerackers, mflerackers@androme.be
 *		John Scipione, jscipione@gmail.com
 */


#include <ScrollBar.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ControlLook.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <OS.h>
#include <Shape.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


//#define TRACE_SCROLLBAR
#ifdef TRACE_SCROLLBAR
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...)
#endif


typedef enum {
	ARROW_LEFT = 0,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	ARROW_NONE
} arrow_direction;


#define SBC_SCROLLBYVALUE	0
#define SBC_SETDOUBLE		1
#define SBC_SETPROPORTIONAL	2
#define SBC_SETSTYLE		3

// Quick constants for determining which arrow is down and are defined with
// respect to double arrow mode. ARROW1 and ARROW4 refer to the outer pair of
// arrows and ARROW2 and ARROW3 refer to the inner ones. ARROW1 points left/up
// and ARROW4 points right/down.
#define ARROW1	0
#define ARROW2	1
#define ARROW3	2
#define ARROW4	3
#define THUMB	4
#define NOARROW	-1


static const bigtime_t kRepeatDelay = 300000;


// Because the R5 version kept a lot of data on server-side, we need to kludge
// our way into binary compatibility
class BScrollBar::Private {
public:
	Private(BScrollBar* scrollBar)
	:
	fScrollBar(scrollBar),
	fEnabled(true),
	fRepeaterThread(-1),
	fExitRepeater(false),
	fRepeaterDelay(0),
	fThumbFrame(0.0, 0.0, -1.0, -1.0),
	fDoRepeat(false),
	fClickOffset(0.0, 0.0),
	fThumbInc(0.0),
	fStopValue(0.0),
	fUpArrowsEnabled(true),
	fDownArrowsEnabled(true),
	fBorderHighlighted(false),
	fButtonDown(NOARROW)
	{
#ifdef TEST_MODE
			fScrollBarInfo.proportional = true;
			fScrollBarInfo.double_arrows = true;
			fScrollBarInfo.knob = 0;
			fScrollBarInfo.min_knob_size = 15;
#else
			get_scroll_bar_info(&fScrollBarInfo);
#endif
	}

	~Private()
	{
		if (fRepeaterThread >= 0) {
			status_t dummy;
			fExitRepeater = true;
			wait_for_thread(fRepeaterThread, &dummy);
		}
	}

	void DrawScrollBarButton(BScrollBar* owner, arrow_direction direction,
		BRect frame, bool down = false);

	static int32 button_repeater_thread(void* data);

	int32 ButtonRepeaterThread();

	BScrollBar*			fScrollBar;
	bool				fEnabled;

	// TODO: This should be a static, initialized by
	// _init_interface_kit() at application startup-time,
	// like BMenu::sMenuInfo
	scroll_bar_info		fScrollBarInfo;

	thread_id			fRepeaterThread;
	volatile bool		fExitRepeater;
	bigtime_t			fRepeaterDelay;

	BRect				fThumbFrame;
	volatile bool		fDoRepeat;
	BPoint				fClickOffset;

	float				fThumbInc;
	float				fStopValue;

	bool				fUpArrowsEnabled;
	bool				fDownArrowsEnabled;

	bool				fBorderHighlighted;

	int8				fButtonDown;
};


// This thread is spawned when a button is initially pushed and repeatedly scrolls
// the scrollbar by a little bit after a short delay
int32
BScrollBar::Private::button_repeater_thread(void* data)
{
	BScrollBar::Private* privateData = (BScrollBar::Private*)data;
	return privateData->ButtonRepeaterThread();
}


int32
BScrollBar::Private::ButtonRepeaterThread()
{
	// Wait a bit before auto scrolling starts. As long as the user releases
	// and presses the button again while the repeat delay has not yet
	// triggered, the value is pushed into the future, so we need to loop such
	// that repeating starts at exactly the correct delay after the last
	// button press.
	while (fRepeaterDelay > system_time() && !fExitRepeater)
		snooze_until(fRepeaterDelay, B_SYSTEM_TIMEBASE);

	// repeat loop
	while (!fExitRepeater) {
		if (fScrollBar->LockLooper()) {
			if (fDoRepeat) {
				float value = fScrollBar->Value() + fThumbInc;
				if (fButtonDown == NOARROW) {
					// in this case we want to stop when we're under the mouse
					if (fThumbInc > 0.0 && value <= fStopValue)
						fScrollBar->SetValue(value);
					if (fThumbInc < 0.0 && value >= fStopValue)
						fScrollBar->SetValue(value);
				} else
					fScrollBar->SetValue(value);
			}

			fScrollBar->UnlockLooper();
		}

		snooze(25000);
	}

	// tell scrollbar we're gone
	if (fScrollBar->LockLooper()) {
		fRepeaterThread = -1;
		fScrollBar->UnlockLooper();
	}

	return 0;
}


//	#pragma mark - BScrollBar


BScrollBar::BScrollBar(BRect frame, const char* name, BView* target,
	float min, float max, orientation direction)
	:
	BView(frame, name, B_FOLLOW_NONE,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	fMin(min),
	fMax(max),
	fSmallStep(1.0f),
	fLargeStep(10.0f),
	fValue(0),
	fProportion(0.0f),
	fTarget(NULL),
	fOrientation(direction),
	fTargetName(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);

	fPrivateData = new BScrollBar::Private(this);

	SetTarget(target);
	SetEventMask(B_NO_POINTER_HISTORY);

	_UpdateThumbFrame();
	_UpdateArrowButtons();

	SetResizingMode(direction == B_VERTICAL
		? B_FOLLOW_TOP_BOTTOM | B_FOLLOW_RIGHT
		: B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
}


BScrollBar::BScrollBar(const char* name, BView* target,
	float min, float max, orientation direction)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	fMin(min),
	fMax(max),
	fSmallStep(1.0f),
	fLargeStep(10.0f),
	fValue(0),
	fProportion(0.0f),
	fTarget(NULL),
	fOrientation(direction),
	fTargetName(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);

	fPrivateData = new BScrollBar::Private(this);

	SetTarget(target);
	SetEventMask(B_NO_POINTER_HISTORY);

	_UpdateThumbFrame();
	_UpdateArrowButtons();
}


BScrollBar::BScrollBar(BMessage* data)
	:
	BView(data),
	fTarget(NULL),
	fTargetName(NULL)
{
	fPrivateData = new BScrollBar::Private(this);

	// TODO: Does the BeOS implementation try to find the target
	// by name again? Does it archive the name at all?
	if (data->FindFloat("_range", 0, &fMin) < B_OK)
		fMin = 0.0f;

	if (data->FindFloat("_range", 1, &fMax) < B_OK)
		fMax = 0.0f;

	if (data->FindFloat("_steps", 0, &fSmallStep) < B_OK)
		fSmallStep = 1.0f;

	if (data->FindFloat("_steps", 1, &fLargeStep) < B_OK)
		fLargeStep = 10.0f;

	if (data->FindFloat("_val", &fValue) < B_OK)
		fValue = 0.0;

	int32 orientation;
	if (data->FindInt32("_orient", &orientation) < B_OK) {
		fOrientation = B_VERTICAL;
		if ((Flags() & B_SUPPORTS_LAYOUT) == 0) {
			// just to make sure
			SetResizingMode(fOrientation == B_VERTICAL
				? B_FOLLOW_TOP_BOTTOM | B_FOLLOW_RIGHT
				: B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
		}
	} else
		fOrientation = (enum orientation)orientation;

	if (data->FindFloat("_prop", &fProportion) < B_OK)
		fProportion = 0.0;

	_UpdateThumbFrame();
	_UpdateArrowButtons();
}


BScrollBar::~BScrollBar()
{
	SetTarget((BView*)NULL);
	delete fPrivateData;
}


BArchivable*
BScrollBar::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BScrollBar"))
		return new BScrollBar(data);
	return NULL;
}


status_t
BScrollBar::Archive(BMessage* data, bool deep) const
{
	status_t err = BView::Archive(data, deep);
	if (err != B_OK)
		return err;
	err = data->AddFloat("_range", fMin);
	if (err != B_OK)
		return err;
	err = data->AddFloat("_range", fMax);
	if (err != B_OK)
		return err;
	err = data->AddFloat("_steps", fSmallStep);
	if (err != B_OK)
		return err;
	err = data->AddFloat("_steps", fLargeStep);
	if (err != B_OK)
		return err;
	err = data->AddFloat("_val", fValue);
	if (err != B_OK)
		return err;
	err = data->AddInt32("_orient", (int32)fOrientation);
	if (err != B_OK)
		return err;
	err = data->AddFloat("_prop", fProportion);

	return err;
}


// #pragma mark -


void
BScrollBar::AttachedToWindow()
{
}

/*
	From the BeBook (on ValueChanged()):

Responds to a notification that the value of the scroll bar has changed to
newValue. For a horizontal scroll bar, this function interprets newValue
as the coordinate value that should be at the left side of the target
view's bounds rectangle. For a vertical scroll bar, it interprets
newValue as the coordinate value that should be at the top of the rectangle.
It calls ScrollTo() to scroll the target's contents into position, unless
they have already been scrolled.

ValueChanged() is called as the result both of user actions
(B_VALUE_CHANGED messages received from the Application Server) and of
programmatic ones. Programmatically, scrolling can be initiated by the
target view (calling ScrollTo()) or by the BScrollBar
(calling SetValue() or SetRange()).

In all these cases, the target view and the scroll bars need to be kept
in synch. This is done by a chain of function calls: ValueChanged() calls
ScrollTo(), which in turn calls SetValue(), which then calls
ValueChanged() again. It's up to ValueChanged() to get off this
merry-go-round, which it does by checking the target view's bounds
rectangle. If newValue already matches the left or top side of the
bounds rectangle, if forgoes calling ScrollTo().

ValueChanged() does nothing if a target BView hasn't been set—or
if the target has been set by name, but the name doesn't correspond to
an actual BView within the scroll bar's window.

*/


void
BScrollBar::SetValue(float value)
{
	if (value > fMax)
		value = fMax;
	else if (value < fMin)
		value = fMin;
	else if (isnan(value))
		return;

	value = roundf(value);
	if (value == fValue)
		return;

	TRACE("BScrollBar(%s)::SetValue(%.1f)\n", Name(), value);

	fValue = value;

	_UpdateThumbFrame();
	_UpdateArrowButtons();

	ValueChanged(fValue);
}


float
BScrollBar::Value() const
{
	return fValue;
}


void
BScrollBar::ValueChanged(float newValue)
{
	TRACE("BScrollBar(%s)::ValueChanged(%.1f)\n", Name(), newValue);

	if (fTarget != NULL) {
		// cache target bounds
		BRect targetBounds = fTarget->Bounds();
		// if vertical, check bounds top and scroll if different from newValue
		if (fOrientation == B_VERTICAL && targetBounds.top != newValue)
			fTarget->ScrollBy(0.0, newValue - targetBounds.top);

		// if horizontal, check bounds left and scroll if different from newValue
		if (fOrientation == B_HORIZONTAL && targetBounds.left != newValue)
			fTarget->ScrollBy(newValue - targetBounds.left, 0.0);
	}

	TRACE(" -> %.1f\n", newValue);

	SetValue(newValue);
}


void
BScrollBar::SetProportion(float value)
{
	if (value < 0.0f)
		value = 0.0f;
	else if (value > 1.0f)
		value = 1.0f;

	if (value == fProportion)
		return;

	TRACE("BScrollBar(%s)::SetProportion(%.1f)\n", Name(), value);

	bool oldEnabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	fProportion = value;

	bool newEnabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	_UpdateThumbFrame();

	if (oldEnabled != newEnabled)
		Invalidate();
}


float
BScrollBar::Proportion() const
{
	return fProportion;
}


void
BScrollBar::SetRange(float min, float max)
{
	if (min > max || isnanf(min) || isnanf(max)
		|| isinff(min) || isinff(max)) {
		min = 0.0f;
		max = 0.0f;
	}

	min = roundf(min);
	max = roundf(max);

	if (fMin == min && fMax == max)
		return;
	TRACE("BScrollBar(%s)::SetRange(min=%.1f, max=%.1f)\n", Name(), min, max);

	fMin = min;
	fMax = max;

	if (fValue < fMin || fValue > fMax)
		SetValue(fValue);
	else {
		_UpdateThumbFrame();
		Invalidate();
	}
}


void
BScrollBar::GetRange(float* min, float* max) const
{
	if (min != NULL)
		*min = fMin;
	if (max != NULL)
		*max = fMax;
}


void
BScrollBar::SetSteps(float smallStep, float largeStep)
{
	// Under R5, steps can be set only after being attached to a window,
	// probably because the data is kept server-side. We'll just remove
	// that limitation... :P

	// The BeBook also says that we need to specify an integer value even
	// though the step values are floats. For the moment, we'll just make
	// sure that they are integers
	smallStep = roundf(smallStep);
	largeStep = roundf(largeStep);
	if (fSmallStep == smallStep && fLargeStep == largeStep)
		return;

	TRACE("BScrollBar(%s)::SetSteps(small=%.1f, large=%.1f)\n", Name(),
		smallStep, largeStep);

	fSmallStep = smallStep;
	fLargeStep = largeStep;

	if (fProportion == 0.0) {
		// special case, proportion is based on fLargeStep if it was never
		// set, so it means we need to invalidate here
		_UpdateThumbFrame();
		Invalidate();
	}

	// TODO: test use of fractional values and make them work properly if
	// they don't
}


void
BScrollBar::GetSteps(float* smallStep, float* largeStep) const
{
	if (smallStep != NULL)
		*smallStep = fSmallStep;

	if (largeStep != NULL)
		*largeStep = fLargeStep;
}


void
BScrollBar::SetTarget(BView* target)
{
	if (fTarget) {
		// unset the previous target's scrollbar pointer
		if (fOrientation == B_VERTICAL)
			fTarget->fVerScroller = NULL;
		else
			fTarget->fHorScroller = NULL;
	}

	fTarget = target;
	free(fTargetName);

	if (fTarget) {
		fTargetName = strdup(target->Name());

		if (fOrientation == B_VERTICAL)
			fTarget->fVerScroller = this;
		else
			fTarget->fHorScroller = this;
	} else
		fTargetName = NULL;
}


void
BScrollBar::SetTarget(const char* targetName)
{
	// NOTE 1: BeOS implementation crashes for targetName == NULL
	// NOTE 2: BeOS implementation also does not modify the target
	// if it can't be found
	if (!targetName)
		return;

	if (!Window())
		debugger("Method requires window and doesn't have one");

	BView* target = Window()->FindView(targetName);
	if (target)
		SetTarget(target);
}


BView*
BScrollBar::Target() const
{
	return fTarget;
}


void
BScrollBar::SetOrientation(orientation orientation)
{
	if (fOrientation == orientation)
		return;

	fOrientation = orientation;
	InvalidateLayout();
	Invalidate();
}


orientation
BScrollBar::Orientation() const
{
	return fOrientation;
}


status_t
BScrollBar::SetBorderHighlighted(bool state)
{
	if (fPrivateData->fBorderHighlighted == state)
		return B_OK;

	fPrivateData->fBorderHighlighted = state;

	BRect dirty(Bounds());
	if (fOrientation == B_HORIZONTAL)
		dirty.bottom = dirty.top;
	else
		dirty.right = dirty.left;

	Invalidate(dirty);

	return B_OK;
}


void
BScrollBar::MessageReceived(BMessage* message)
{
	switch(message->what) {
		case B_VALUE_CHANGED:
		{
			int32 value;
			if (message->FindInt32("value", &value) == B_OK)
				ValueChanged(value);

			break;
		}

		case B_MOUSE_WHEEL_CHANGED:
		{
			// Must handle this here since BView checks for the existence of
			// scrollbars, which a scrollbar itself does not have
			float deltaX = 0.0f;
			float deltaY = 0.0f;
			message->FindFloat("be:wheel_delta_x", &deltaX);
			message->FindFloat("be:wheel_delta_y", &deltaY);

			if (deltaX == 0.0f && deltaY == 0.0f)
				break;

			if (deltaX != 0.0f && deltaY == 0.0f)
				deltaY = deltaX;

			ScrollWithMouseWheelDelta(this, deltaY);
		}

		default:
			BView::MessageReceived(message);
	}
}


void
BScrollBar::MouseDown(BPoint where)
{
	if (!fPrivateData->fEnabled || fMin == fMax)
		return;

	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

	int32 buttons;
	if (Looper() == NULL || Looper()->CurrentMessage() == NULL
		|| Looper()->CurrentMessage()->FindInt32("buttons", &buttons) != B_OK) {
		buttons = B_PRIMARY_MOUSE_BUTTON;
	}

	if (buttons & B_SECONDARY_MOUSE_BUTTON) {
		// special absolute scrolling: move thumb to where we clicked
		fPrivateData->fButtonDown = THUMB;
		fPrivateData->fClickOffset = fPrivateData->fThumbFrame.LeftTop() - where;
		if (Orientation() == B_HORIZONTAL)
			fPrivateData->fClickOffset.x = -fPrivateData->fThumbFrame.Width() / 2;
		else
			fPrivateData->fClickOffset.y = -fPrivateData->fThumbFrame.Height() / 2;

		SetValue(_ValueFor(where + fPrivateData->fClickOffset));
		return;
	}

	// hit test for the thumb
	if (fPrivateData->fThumbFrame.Contains(where)) {
		fPrivateData->fButtonDown = THUMB;
		fPrivateData->fClickOffset = fPrivateData->fThumbFrame.LeftTop() - where;
		Invalidate(fPrivateData->fThumbFrame);
		return;
	}

	// hit test for arrows or empty area
	float scrollValue = 0.0;

	// pressing the shift key scrolls faster
	float buttonStepSize
		= (modifiers() & B_SHIFT_KEY) != 0 ? fLargeStep : fSmallStep;

	fPrivateData->fButtonDown = _ButtonFor(where);
	switch (fPrivateData->fButtonDown) {
		case ARROW1:
			scrollValue = -buttonStepSize;
			break;

		case ARROW2:
			scrollValue = buttonStepSize;
			break;

		case ARROW3:
			scrollValue = -buttonStepSize;
			break;

		case ARROW4:
			scrollValue = buttonStepSize;
			break;

		case NOARROW:
			// we hit the empty area, figure out which side of the thumb
			if (fOrientation == B_VERTICAL) {
				if (where.y < fPrivateData->fThumbFrame.top)
					scrollValue = -fLargeStep;
				else
					scrollValue = fLargeStep;
			} else {
				if (where.x < fPrivateData->fThumbFrame.left)
					scrollValue = -fLargeStep;
				else
					scrollValue = fLargeStep;
			}
			_UpdateTargetValue(where);
			break;
	}
	if (scrollValue != 0.0) {
		SetValue(fValue + scrollValue);
		Invalidate(_ButtonRectFor(fPrivateData->fButtonDown));

		// launch the repeat thread
		if (fPrivateData->fRepeaterThread == -1) {
			fPrivateData->fExitRepeater = false;
			fPrivateData->fRepeaterDelay = system_time() + kRepeatDelay;
			fPrivateData->fThumbInc = scrollValue;
			fPrivateData->fDoRepeat = true;
			fPrivateData->fRepeaterThread
				= spawn_thread(fPrivateData->button_repeater_thread,
							   "scroll repeater", B_NORMAL_PRIORITY, fPrivateData);
			resume_thread(fPrivateData->fRepeaterThread);
		} else {
			fPrivateData->fExitRepeater = false;
			fPrivateData->fRepeaterDelay = system_time() + kRepeatDelay;
			fPrivateData->fDoRepeat = true;
		}
	}
}


void
BScrollBar::MouseUp(BPoint pt)
{
	if (fPrivateData->fButtonDown == THUMB)
		Invalidate(fPrivateData->fThumbFrame);
	else
		Invalidate(_ButtonRectFor(fPrivateData->fButtonDown));

	fPrivateData->fButtonDown = NOARROW;
	fPrivateData->fExitRepeater = true;
	fPrivateData->fDoRepeat = false;
}


void
BScrollBar::MouseMoved(BPoint where, uint32 transit, const BMessage* message)
{
	if (!fPrivateData->fEnabled || fMin >= fMax || fProportion >= 1.0f
		|| fProportion < 0.0f) {
		return;
	}

	if (fPrivateData->fButtonDown != NOARROW) {
		if (fPrivateData->fButtonDown == THUMB) {
			SetValue(_ValueFor(where + fPrivateData->fClickOffset));
		} else {
			// suspend the repeating if the mouse is not over the button
			bool repeat = _ButtonRectFor(fPrivateData->fButtonDown).Contains(
				where);
			if (fPrivateData->fDoRepeat != repeat) {
				fPrivateData->fDoRepeat = repeat;
				Invalidate(_ButtonRectFor(fPrivateData->fButtonDown));
			}
		}
	} else {
		// update the value at which we want to stop repeating
		if (fPrivateData->fDoRepeat) {
			_UpdateTargetValue(where);
			// we might have to turn arround
			if ((fValue < fPrivateData->fStopValue
					&& fPrivateData->fThumbInc < 0)
				|| (fValue > fPrivateData->fStopValue
					&& fPrivateData->fThumbInc > 0)) {
				fPrivateData->fThumbInc = -fPrivateData->fThumbInc;
			}
		}
	}
}


void
BScrollBar::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BScrollBar::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	rgb_color normal = ui_color(B_PANEL_BACKGROUND_COLOR);

	// stroke a dark frame arround the entire scrollbar
	// (independent of enabled state)
	// take care of border highlighting (scroll target is focus view)
	SetHighColor(tint_color(normal, B_DARKEN_2_TINT));
	if (fPrivateData->fBorderHighlighted && fPrivateData->fEnabled) {
		rgb_color borderColor = HighColor();
		rgb_color highlightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		BeginLineArray(4);
		AddLine(BPoint(bounds.left + 1, bounds.bottom),
			BPoint(bounds.right, bounds.bottom), borderColor);
		AddLine(BPoint(bounds.right, bounds.top + 1),
			BPoint(bounds.right, bounds.bottom - 1), borderColor);
		if (fOrientation == B_HORIZONTAL) {
			AddLine(BPoint(bounds.left, bounds.top + 1),
				BPoint(bounds.left, bounds.bottom), borderColor);
		} else {
			AddLine(BPoint(bounds.left, bounds.top),
				BPoint(bounds.left, bounds.bottom), highlightColor);
		}
		if (fOrientation == B_HORIZONTAL) {
			AddLine(BPoint(bounds.left, bounds.top),
				BPoint(bounds.right, bounds.top), highlightColor);
		} else {
			AddLine(BPoint(bounds.left + 1, bounds.top),
				BPoint(bounds.right, bounds.top), borderColor);
		}
		EndLineArray();
	} else
		StrokeRect(bounds);

	bounds.InsetBy(1.0f, 1.0f);

	bool enabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	rgb_color light, dark, dark1, dark2;
	if (enabled) {
		light = tint_color(normal, B_LIGHTEN_MAX_TINT);
		dark = tint_color(normal, B_DARKEN_3_TINT);
		dark1 = tint_color(normal, B_DARKEN_1_TINT);
		dark2 = tint_color(normal, B_DARKEN_2_TINT);
	} else {
		light = tint_color(normal, B_LIGHTEN_MAX_TINT);
		dark = tint_color(normal, B_DARKEN_2_TINT);
		dark1 = tint_color(normal, B_LIGHTEN_2_TINT);
		dark2 = tint_color(normal, B_LIGHTEN_1_TINT);
	}

	SetDrawingMode(B_OP_OVER);

	BRect thumbBG = bounds;
	bool doubleArrows = _DoubleArrows();

	// Draw arrows
	if (fOrientation == B_HORIZONTAL) {
		BRect buttonFrame(bounds.left, bounds.top,
			bounds.left + bounds.Height(), bounds.bottom);

		_DrawArrowButton(ARROW_LEFT, doubleArrows, buttonFrame, updateRect,
			enabled, fPrivateData->fButtonDown == ARROW1);

		if (doubleArrows) {
			buttonFrame.OffsetBy(bounds.Height() + 1, 0.0f);
			_DrawArrowButton(ARROW_RIGHT, doubleArrows, buttonFrame, updateRect,
				enabled, fPrivateData->fButtonDown == ARROW2);

			buttonFrame.OffsetTo(bounds.right - ((bounds.Height() * 2) + 1),
				bounds.top);
			_DrawArrowButton(ARROW_LEFT, doubleArrows, buttonFrame, updateRect,
				enabled, fPrivateData->fButtonDown == ARROW3);

			thumbBG.left += bounds.Height() * 2 + 2;
			thumbBG.right -= bounds.Height() * 2 + 2;
		} else {
			thumbBG.left += bounds.Height() + 1;
			thumbBG.right -= bounds.Height() + 1;
		}

		buttonFrame.OffsetTo(bounds.right - bounds.Height(), bounds.top);
		_DrawArrowButton(ARROW_RIGHT, doubleArrows, buttonFrame, updateRect,
			enabled, fPrivateData->fButtonDown == ARROW4);
	} else {
		BRect buttonFrame(bounds.left, bounds.top, bounds.right,
			bounds.top + bounds.Width());

		_DrawArrowButton(ARROW_UP, doubleArrows, buttonFrame, updateRect,
			enabled, fPrivateData->fButtonDown == ARROW1);

		if (doubleArrows) {
			buttonFrame.OffsetBy(0.0f, bounds.Width() + 1);
			_DrawArrowButton(ARROW_DOWN, doubleArrows, buttonFrame, updateRect,
				enabled, fPrivateData->fButtonDown == ARROW2);

			buttonFrame.OffsetTo(bounds.left, bounds.bottom
				- ((bounds.Width() * 2) + 1));
			_DrawArrowButton(ARROW_UP, doubleArrows, buttonFrame, updateRect,
				enabled, fPrivateData->fButtonDown == ARROW3);

			thumbBG.top += bounds.Width() * 2 + 2;
			thumbBG.bottom -= bounds.Width() * 2 + 2;
		} else {
			thumbBG.top += bounds.Width() + 1;
			thumbBG.bottom -= bounds.Width() + 1;
		}

		buttonFrame.OffsetTo(bounds.left, bounds.bottom - bounds.Width());
		_DrawArrowButton(ARROW_DOWN, doubleArrows, buttonFrame, updateRect,
			enabled, fPrivateData->fButtonDown == ARROW4);
	}

	SetDrawingMode(B_OP_COPY);

	// background for thumb area
	BRect rect(fPrivateData->fThumbFrame);

	SetHighColor(dark1);

	uint32 flags = 0;
	if (!enabled)
		flags |= BControlLook::B_DISABLED;

	// fill background besides the thumb
	if (fOrientation == B_HORIZONTAL) {
		BRect leftOfThumb(thumbBG.left, thumbBG.top, rect.left - 1,
			thumbBG.bottom);
		BRect rightOfThumb(rect.right + 1, thumbBG.top, thumbBG.right,
			thumbBG.bottom);

		be_control_look->DrawScrollBarBackground(this, leftOfThumb,
			rightOfThumb, updateRect, normal, flags, fOrientation);
	} else {
		BRect topOfThumb(thumbBG.left, thumbBG.top,
			thumbBG.right, rect.top - 1);

		BRect bottomOfThumb(thumbBG.left, rect.bottom + 1,
			thumbBG.right, thumbBG.bottom);

		be_control_look->DrawScrollBarBackground(this, topOfThumb,
			bottomOfThumb, updateRect, normal, flags, fOrientation);
	}

	rgb_color thumbColor = ui_color(B_SCROLL_BAR_THUMB_COLOR);

	// Draw scroll thumb
	if (enabled) {
		// fill the clickable surface of the thumb
		be_control_look->DrawButtonBackground(this, rect, updateRect,
			thumbColor, 0, BControlLook::B_ALL_BORDERS, fOrientation);
		// TODO: Add the other thumb styles - dots and lines
	} else {
		if (fMin >= fMax || fProportion >= 1.0f || fProportion < 0.0f) {
			// we cannot scroll at all
			_DrawDisabledBackground(thumbBG, light, dark, dark1);
		} else {
			// we could scroll, but we're simply disabled
			float bgTint = 1.06;
			rgb_color bgLight = tint_color(light, bgTint * 3);
			rgb_color bgShadow = tint_color(dark, bgTint);
			rgb_color bgFill = tint_color(dark1, bgTint);
			if (fOrientation == B_HORIZONTAL) {
				// left of thumb
				BRect besidesThumb(thumbBG);
				besidesThumb.right = rect.left - 1;
				_DrawDisabledBackground(besidesThumb, bgLight, bgShadow, bgFill);
				// right of thumb
				besidesThumb.left = rect.right + 1;
				besidesThumb.right = thumbBG.right;
				_DrawDisabledBackground(besidesThumb, bgLight, bgShadow, bgFill);
			} else {
				// above thumb
				BRect besidesThumb(thumbBG);
				besidesThumb.bottom = rect.top - 1;
				_DrawDisabledBackground(besidesThumb, bgLight, bgShadow, bgFill);
				// below thumb
				besidesThumb.top = rect.bottom + 1;
				besidesThumb.bottom = thumbBG.bottom;
				_DrawDisabledBackground(besidesThumb, bgLight, bgShadow, bgFill);
			}
			// thumb bevel
			BeginLineArray(4);
				AddLine(BPoint(rect.left, rect.bottom),
						BPoint(rect.left, rect.top), light);
				AddLine(BPoint(rect.left + 1, rect.top),
						BPoint(rect.right, rect.top), light);
				AddLine(BPoint(rect.right, rect.top + 1),
						BPoint(rect.right, rect.bottom), dark2);
				AddLine(BPoint(rect.right - 1, rect.bottom),
						BPoint(rect.left + 1, rect.bottom), dark2);
			EndLineArray();
			// thumb fill
			rect.InsetBy(1.0, 1.0);
			SetHighColor(dark1);
			FillRect(rect);
		}
	}
}


void
BScrollBar::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


void
BScrollBar::FrameResized(float newWidth, float newHeight)
{
	_UpdateThumbFrame();
}


BHandler*
BScrollBar::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	return BView::ResolveSpecifier(message, index, specifier, form, property);
}


void
BScrollBar::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


void
BScrollBar::GetPreferredSize(float* _width, float* _height)
{
	if (fOrientation == B_VERTICAL) {
		if (_width)
			*_width = B_V_SCROLL_BAR_WIDTH;

		if (_height)
			*_height = Bounds().Height();
	} else if (fOrientation == B_HORIZONTAL) {
		if (_width)
			*_width = Bounds().Width();

		if (_height)
			*_height = B_H_SCROLL_BAR_HEIGHT;
	}
}


void
BScrollBar::MakeFocus(bool state)
{
	BView::MakeFocus(state);
}


void
BScrollBar::AllAttached()
{
	BView::AllAttached();
}


void
BScrollBar::AllDetached()
{
	BView::AllDetached();
}


status_t
BScrollBar::GetSupportedSuites(BMessage* message)
{
	return BView::GetSupportedSuites(message);
}


BSize
BScrollBar::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), _MinSize());
}


BSize
BScrollBar::MaxSize()
{
	BSize maxSize = _MinSize();
	if (fOrientation == B_HORIZONTAL)
		maxSize.width = B_SIZE_UNLIMITED;
	else
		maxSize.height = B_SIZE_UNLIMITED;
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), maxSize);
}


BSize
BScrollBar::PreferredSize()
{
	BSize preferredSize = _MinSize();
	if (fOrientation == B_HORIZONTAL)
		preferredSize.width *= 2;
	else
		preferredSize.height *= 2;

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), preferredSize);
}


status_t
BScrollBar::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BScrollBar::MinSize();

			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BScrollBar::MaxSize();

			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BScrollBar::PreferredSize();

			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BScrollBar::LayoutAlignment();

			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BScrollBar::HasHeightForWidth();

			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BScrollBar::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);

			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BScrollBar::SetLayout(data->layout);

			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BScrollBar::LayoutInvalidated(data->descendants);

			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BScrollBar::DoLayout();

			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


#if DISABLES_ON_WINDOW_DEACTIVATION
void
BScrollBar::WindowActivated(bool active)
{
	fPrivateData->fEnabled = active;
	Invalidate();
}
#endif // DISABLES_ON_WINDOW_DEACTIVATION


void BScrollBar::_ReservedScrollBar1() {}
void BScrollBar::_ReservedScrollBar2() {}
void BScrollBar::_ReservedScrollBar3() {}
void BScrollBar::_ReservedScrollBar4() {}


BScrollBar&
BScrollBar::operator=(const BScrollBar&)
{
	return *this;
}


bool
BScrollBar::_DoubleArrows() const
{
	if (!fPrivateData->fScrollBarInfo.double_arrows)
		return false;

	// if there is not enough room, switch to single arrows even though
	// double arrows is specified
	if (fOrientation == B_HORIZONTAL) {
		return Bounds().Width() > (Bounds().Height() + 1) * 4
			+ fPrivateData->fScrollBarInfo.min_knob_size * 2;
	} else {
		return Bounds().Height() > (Bounds().Width() + 1) * 4
			+ fPrivateData->fScrollBarInfo.min_knob_size * 2;
	}
}


void
BScrollBar::_UpdateThumbFrame()
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0, 1.0);

	BRect oldFrame = fPrivateData->fThumbFrame;
	fPrivateData->fThumbFrame = bounds;
	float minSize = fPrivateData->fScrollBarInfo.min_knob_size;
	float maxSize;
	float buttonSize;

	// assume square buttons
	if (fOrientation == B_VERTICAL) {
		maxSize = bounds.Height();
		buttonSize = bounds.Width() + 1.0;
	} else {
		maxSize = bounds.Width();
		buttonSize = bounds.Height() + 1.0;
	}

	if (_DoubleArrows()) {
		// subtract the size of four buttons
		maxSize -= buttonSize * 4;
	} else {
		// subtract the size of two buttons
		maxSize -= buttonSize * 2;
	}
	// visual adjustments (room for darker line between thumb and buttons)
	maxSize--;

	float thumbSize;
	if (fPrivateData->fScrollBarInfo.proportional) {
		float proportion = fProportion;
		if (fMin >= fMax || proportion > 1.0 || proportion < 0.0)
			proportion = 1.0;

		if (proportion == 0.0) {
			// Special case a proportion of 0.0, use the large step value
			// in that case (NOTE: fMin == fMax already handled above)
			// This calculation is based on the assumption that "large step"
			// scrolls by one "page size".
			proportion = fLargeStep / (2 * (fMax - fMin));
			if (proportion > 1.0)
				proportion = 1.0;
		}
		thumbSize = maxSize * proportion;
		if (thumbSize < minSize)
			thumbSize = minSize;
	} else
		thumbSize = minSize;

	thumbSize = floorf(thumbSize + 0.5);
	thumbSize--;

	// the thumb can be scrolled within the remaining area "maxSize - thumbSize - 1.0"	
	float offset = 0.0;
	if (fMax > fMin) {
		offset = floorf(((fValue - fMin) / (fMax - fMin))
			* (maxSize - thumbSize - 1.0));
	}

	if (_DoubleArrows()) {
		offset += buttonSize * 2;
	} else
		offset += buttonSize;

	// visual adjustments (room for darker line between thumb and buttons)
	offset++;

	if (fOrientation == B_VERTICAL) {
		fPrivateData->fThumbFrame.bottom = fPrivateData->fThumbFrame.top
			+ thumbSize;
		fPrivateData->fThumbFrame.OffsetBy(0.0, offset);
	} else {
		fPrivateData->fThumbFrame.right = fPrivateData->fThumbFrame.left
			+ thumbSize;
		fPrivateData->fThumbFrame.OffsetBy(offset, 0.0);
	}

	if (Window() != NULL) {
		BRect invalid = oldFrame.IsValid()
			? oldFrame | fPrivateData->fThumbFrame
			: fPrivateData->fThumbFrame;
		// account for those two dark lines
		if (fOrientation == B_HORIZONTAL)
			invalid.InsetBy(-2.0, 0.0);
		else
			invalid.InsetBy(0.0, -2.0);

		Invalidate(invalid);
	}
}


float
BScrollBar::_ValueFor(BPoint where) const
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0f, 1.0f);

	float offset;
	float thumbSize;
	float maxSize;
	float buttonSize;

	if (fOrientation == B_VERTICAL) {
		offset = where.y;
		thumbSize = fPrivateData->fThumbFrame.Height();
		maxSize = bounds.Height();
		buttonSize = bounds.Width() + 1.0f;
	} else {
		offset = where.x;
		thumbSize = fPrivateData->fThumbFrame.Width();
		maxSize = bounds.Width();
		buttonSize = bounds.Height() + 1.0f;
	}

	if (_DoubleArrows()) {
		// subtract the size of four buttons
		maxSize -= buttonSize * 4;
		// convert point to inside of area between buttons
		offset -= buttonSize * 2;
	} else {
		// subtract the size of two buttons
		maxSize -= buttonSize * 2;
		// convert point to inside of area between buttons
		offset -= buttonSize;
	}
	// visual adjustments (room for darker line between thumb and buttons)
	maxSize--;
	offset++;

	return roundf(fMin + (offset / (maxSize - thumbSize)
		* (fMax - fMin + 1.0f)));
}


int32
BScrollBar::_ButtonFor(BPoint where) const
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0f, 1.0f);

	float buttonSize = fOrientation == B_VERTICAL
		? bounds.Width() + 1.0f
		: bounds.Height() + 1.0f;

	BRect rect(bounds.left, bounds.top,
		bounds.left + buttonSize, bounds.top + buttonSize);

	if (fOrientation == B_VERTICAL) {
		if (rect.Contains(where))
			return ARROW1;

		if (_DoubleArrows()) {
			rect.OffsetBy(0.0, buttonSize);
			if (rect.Contains(where))
				return ARROW2;

			rect.OffsetTo(bounds.left, bounds.bottom - 2 * buttonSize);
			if (rect.Contains(where))
				return ARROW3;
		}
		rect.OffsetTo(bounds.left, bounds.bottom - buttonSize);
		if (rect.Contains(where))
			return ARROW4;
	} else {
		if (rect.Contains(where))
			return ARROW1;

		if (_DoubleArrows()) {
			rect.OffsetBy(buttonSize, 0.0);
			if (rect.Contains(where))
				return ARROW2;

			rect.OffsetTo(bounds.right - 2 * buttonSize, bounds.top);
			if (rect.Contains(where))
				return ARROW3;
		}
		rect.OffsetTo(bounds.right - buttonSize, bounds.top);
		if (rect.Contains(where))
			return ARROW4;
	}

	return NOARROW;
}


BRect
BScrollBar::_ButtonRectFor(int32 button) const
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0f, 1.0f);

	float buttonSize = fOrientation == B_VERTICAL
		? bounds.Width() + 1.0f
		: bounds.Height() + 1.0f;

	BRect rect(bounds.left, bounds.top,
		bounds.left + buttonSize - 1.0f, bounds.top + buttonSize - 1.0f);

	if (fOrientation == B_VERTICAL) {
		switch (button) {
			case ARROW1:
				break;

			case ARROW2:
				rect.OffsetBy(0.0, buttonSize);
				break;

			case ARROW3:
				rect.OffsetTo(bounds.left, bounds.bottom - 2 * buttonSize + 1);
				break;

			case ARROW4:
				rect.OffsetTo(bounds.left, bounds.bottom - buttonSize + 1);
				break;
		}
	} else {
		switch (button) {
			case ARROW1:
				break;

			case ARROW2:
				rect.OffsetBy(buttonSize, 0.0);
				break;

			case ARROW3:
				rect.OffsetTo(bounds.right - 2 * buttonSize + 1, bounds.top);
				break;

			case ARROW4:
				rect.OffsetTo(bounds.right - buttonSize + 1, bounds.top);
				break;
		}
	}

	return rect;
}


void
BScrollBar::_UpdateTargetValue(BPoint where)
{
	if (fOrientation == B_VERTICAL) {
		fPrivateData->fStopValue = _ValueFor(BPoint(where.x, where.y
			- fPrivateData->fThumbFrame.Height() / 2.0));
	} else {
		fPrivateData->fStopValue = _ValueFor(BPoint(where.x
			- fPrivateData->fThumbFrame.Width() / 2.0, where.y));
	}
}


void
BScrollBar::_UpdateArrowButtons()
{
	bool upEnabled = fValue > fMin;
	if (fPrivateData->fUpArrowsEnabled != upEnabled) {
		fPrivateData->fUpArrowsEnabled = upEnabled;
		Invalidate(_ButtonRectFor(ARROW1));
		if (_DoubleArrows())
			Invalidate(_ButtonRectFor(ARROW3));
	}

	bool downEnabled = fValue < fMax;
	if (fPrivateData->fDownArrowsEnabled != downEnabled) {
		fPrivateData->fDownArrowsEnabled = downEnabled;
		Invalidate(_ButtonRectFor(ARROW4));
		if (_DoubleArrows())
			Invalidate(_ButtonRectFor(ARROW2));
	}
}


status_t
control_scrollbar(scroll_bar_info* info, BScrollBar* bar)
{
	if (bar == NULL || info == NULL)
		return B_BAD_VALUE;

	if (bar->fPrivateData->fScrollBarInfo.double_arrows
			!= info->double_arrows) {
		bar->fPrivateData->fScrollBarInfo.double_arrows = info->double_arrows;

		int8 multiplier = (info->double_arrows) ? 1 : -1;

		if (bar->fOrientation == B_VERTICAL) {
			bar->fPrivateData->fThumbFrame.OffsetBy(0, multiplier
				* B_H_SCROLL_BAR_HEIGHT);
		} else {
			bar->fPrivateData->fThumbFrame.OffsetBy(multiplier
				* B_V_SCROLL_BAR_WIDTH, 0);
		}
	}

	bar->fPrivateData->fScrollBarInfo.proportional = info->proportional;

	// TODO: Figure out how proportional relates to the size of the thumb

	// TODO: Add redraw code to reflect the changes

	if (info->knob >= 0 && info->knob <= 2)
		bar->fPrivateData->fScrollBarInfo.knob = info->knob;
	else
		return B_BAD_VALUE;

	if (info->min_knob_size >= SCROLL_BAR_MINIMUM_KNOB_SIZE
			&& info->min_knob_size <= SCROLL_BAR_MAXIMUM_KNOB_SIZE) {
		bar->fPrivateData->fScrollBarInfo.min_knob_size = info->min_knob_size;
	} else
		return B_BAD_VALUE;

	return B_OK;
}


void
BScrollBar::_DrawDisabledBackground(BRect area, const rgb_color& light,
	const rgb_color& dark, const rgb_color& fill)
{
	if (!area.IsValid())
		return;

	if (fOrientation == B_VERTICAL) {
		int32 height = area.IntegerHeight();
		if (height == 0) {
			SetHighColor(dark);
			StrokeLine(area.LeftTop(), area.RightTop());
		} else if (height == 1) {
			SetHighColor(dark);
			FillRect(area);
		} else {
			BeginLineArray(4);
				AddLine(BPoint(area.left, area.top),
						BPoint(area.right, area.top), dark);
				AddLine(BPoint(area.left, area.bottom - 1),
						BPoint(area.left, area.top + 1), light);
				AddLine(BPoint(area.left + 1, area.top + 1),
						BPoint(area.right, area.top + 1), light);
				AddLine(BPoint(area.right, area.bottom),
						BPoint(area.left, area.bottom), dark);
			EndLineArray();
			area.left++;
			area.top += 2;
			area.bottom--;
			if (area.IsValid()) {
				SetHighColor(fill);
				FillRect(area);
			}
		}
	} else {
		int32 width = area.IntegerWidth();
		if (width == 0) {
			SetHighColor(dark);
			StrokeLine(area.LeftBottom(), area.LeftTop());
		} else if (width == 1) {
			SetHighColor(dark);
			FillRect(area);
		} else {
			BeginLineArray(4);
				AddLine(BPoint(area.left, area.bottom),
						BPoint(area.left, area.top), dark);
				AddLine(BPoint(area.left + 1, area.bottom),
						BPoint(area.left + 1, area.top + 1), light);
				AddLine(BPoint(area.left + 1, area.top),
						BPoint(area.right - 1, area.top), light);
				AddLine(BPoint(area.right, area.top),
						BPoint(area.right, area.bottom), dark);
			EndLineArray();
			area.left += 2;
			area.top ++;
			area.right--;
			if (area.IsValid()) {
				SetHighColor(fill);
				FillRect(area);
			}
		}
	}
}


void
BScrollBar::_DrawArrowButton(int32 direction, bool doubleArrows, BRect rect,
	const BRect& updateRect, bool enabled, bool down)
{
	if (!updateRect.Intersects(rect))
		return;

	uint32 flags = 0;
	if (!enabled)
		flags |= BControlLook::B_DISABLED;

	if (down && fPrivateData->fDoRepeat)
		flags |= BControlLook::B_ACTIVATED;

	// TODO: Why does BControlLook need this as the base color for the
	// scrollbar to look right?
	rgb_color baseColor = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_LIGHTEN_1_TINT);

	be_control_look->DrawButtonBackground(this, rect, updateRect, baseColor,
		flags, BControlLook::B_ALL_BORDERS, fOrientation);

	// TODO: Why does BControlLook need this negative inset for the arrow to
	// look right?
	rect.InsetBy(-1.0f, -1.0f);
	be_control_look->DrawArrowShape(this, rect, updateRect,
		baseColor, direction, flags, B_DARKEN_MAX_TINT);
}


BSize
BScrollBar::_MinSize() const
{
	BSize minSize;
	if (fOrientation == B_HORIZONTAL) {
		minSize.width = 2 * B_V_SCROLL_BAR_WIDTH
			+ 2 * fPrivateData->fScrollBarInfo.min_knob_size;
		minSize.height = B_H_SCROLL_BAR_HEIGHT;
	} else {
		minSize.width = B_V_SCROLL_BAR_WIDTH;
		minSize.height = 2 * B_H_SCROLL_BAR_HEIGHT
			+ 2 * fPrivateData->fScrollBarInfo.min_knob_size;
	}

	return minSize;
}
