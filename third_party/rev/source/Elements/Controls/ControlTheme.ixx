module;

export module Rev.Element.ControlTheme;

import Rev.Appearance;

export namespace Rev::Element::ControlTheme {

    using namespace Rev::Appearance;

    // Shared tokens for TextInput, Dropdown, Checkbox, Slider, Button
    //--------------------------------------------------

    Shadow subtleShadow = {
        .color = rgba(15, 23, 42, 0.06),
        .size = Px(-2),
        .blur = 8_px,
        .y = 2_px
    };

    Style Control = {
        .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
        .size = { Grow() },
        .margin = { .top = 4_px, .bottom = 4_px }
    };

    Style Label = {
        .margin = { .bottom = 4_px },
        .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
    };

    Style Field = {
        .overflow = Overflow::Show,
        .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
        .size = { Grow() },
        .margin = { .top = 1_px, .bottom = 1_px },
        .padding = { .left = 9_px, .right = 9_px, .top = 6_px, .bottom = 5_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .radius = 3_px,
            .width = 1_px
        },
        .shadow = subtleShadow
    };

    Style FieldFocus = {
        .applies = { .focus = true },
        .border = {
            .color = rgba(79, 99, 255, 1.0),
            .width = 1_px
        }
    };

    // Background-only override layered after `Field` on a Dropdown's closed box, so
    // a dropdown can read differently (e.g. slightly lighter) than a text field even
    // though both share `Field` for layout/border.
    Style DropdownSurface = {
        .background = { .color = rgba(255, 255, 255, 0.05) }
    };

    Style FieldInner = {
        .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
        .size = { Grow() }
    };

    // DISABLED / LOCKED field: display-only.  A muted, recessed surface with no
    // focus affordance + the default cursor, so the value reads as "shown, not
    // editable".  Applied when the element's disabled style state is set.
    Style FieldDisabled = {
        .applies = { .disabled = true },
        .background = { .color = rgba(241, 245, 249, 1.0) },   // slate-100, recessed
        .border = { .color = rgba(226, 232, 240, 1.0) },
        .cursor = Cursor::Default
    };

    Style FieldText = {
        .size = { Grow() },
        .text = {
            .color = rgba(30, 41, 59, 1.0),
            .size = 13_px,
            // Single-line field: never word-wrap. BreakWord would treat a space
            // as a wrap point AND collapse the field's min-content width to the
            // widest word (see Text::measureText / layoutText), so any value with
            // a space would wrap to a second line and throw off caret/selection.
            .wrap = Wrap::False
        }
    };

    // Muted text for a disabled / locked field.
    Style FieldTextDisabled = {
        .applies = { .disabled = true },
        .text = { .color = rgba(148, 163, 184, 1.0) }   // slate-400
    };

    Style FieldSuffix = {
        .margin = { .left = 6_px },
        .text = {
            .color = rgba(100, 116, 139, 0.72),
            .size = 12_px,
            .wrap = Wrap::False
        }
    };

    Style FieldSuffixDisabled = {
        .applies = { .disabled = true },
        .text = { .color = rgba(148, 163, 184, 0.82) }
    };

    Style Placeholder = {
        .layout = { .position = Position::Absolute },
        .position = { .left = 0_px, .top = 0_px },
        .size = { 100_pct, 100_pct },
        .text = { .color = rgba(148, 163, 184, 1.0), .size = 13_px }
    };

    Style DropdownArrow = {
        .size = { 12_px, 12_px },
        .text = { .color = rgba(100, 116, 139, 1.0) }
    };

    Style OptionsContainer = {
        .visibility = { Visibility::Hidden },
        .overflow = Overflow::Hide,
        .layout = {
            .direction = Axis::Vertical,
            .vertical = Align::End,
            .wrap = Wrap::False,
            .position = Position::Absolute
        },
        .position = { .top = 100_pct },
        .size = { .min = { .width = 100_pct } },
        //.margin = { .top = 6_px },
        .padding = { .left = 4_px, .right = 4_px, .top = 4_px, .bottom = 4_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .radius = 8_px,
            .width = 1_px
        },
        .shadow = subtleShadow,
        .zIndex = +10
    };

    // Same as OptionsContainer but anchored upward:
    // .bottom = 100_pct places the container's bottom at the field's top,
    // so items (justified to Align::End = bottom of the container) stack
    // upward out of the field rather than downward below it.
    Style OptionsContainerUpward = {
        .visibility = { Visibility::Hidden },
        .overflow = Overflow::Hide,
        .layout = {
            .direction = Axis::Vertical,
            .vertical = Align::End,
            .wrap = Wrap::False,
            .position = Position::Absolute
        },
        .position = { .bottom = 100_pct },
        .size = { .min = { .width = 100_pct } },
        .padding = { .left = 4_px, .right = 4_px, .top = 4_px, .bottom = 4_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .radius = 8_px,
            .width = 1_px
        },
        .shadow = subtleShadow,
        .zIndex = +5
    };

    Style Option = {
        .size = { .min = { .width = 100_pct } },
        .padding = { .left = 10_px, .right = 10_px, .top = 6_px, .bottom = 6_px },
        .background = { .color = rgba(255, 255, 255, 0.0), .transition = 100_ms },
        .text = { .color = rgba(30, 41, 59, 1.0), .size = 13_px },
        .cursor = Cursor::Hand
    };

    Style OptionHover = {
        .applies = { .hover = true },
        .background = { .color = rgba(241, 245, 249, 1.0), .transition = 100_ms }
    };

    Style OptionSelected = {
        .background = { .color = rgba(79, 99, 255, 0.08), .transition = 100_ms }
    };

    Style OptionMenuHighlight = {
        .background = { .color = rgba(241, 245, 249, 1.0), .transition = 100_ms }
    };

    Style OptionDisabled = {
        .applies = { .disabled = true },
        .text = { .color = rgba(148, 163, 184, 1.0) },
        .cursor = Cursor::Default
    };

    Style ButtonSecondary = {
        .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
        .padding = { .left = 12_px, .right = 12_px, .top = 6_px, .bottom = 6_px },
        .background = { .color = rgba(255, 255, 255, 1.0), .transition = 120_ms },
        .border = {
            .color = rgba(203, 213, 225, 1.0),
            .radius = 3_px,
            .width = 1_px
        },
        .shadow = subtleShadow,
        .cursor = Cursor::Hand
    };

    Style ButtonSecondaryHover = {
        .applies = { .hover = true, .focus = true },
        .background = { .color = rgba(248, 250, 252, 1.0) },
        .border = { .color = rgba(148, 163, 184, 1.0) }
    };

    Style ButtonSecondaryLabel = {
        .text = { .color = rgba(51, 65, 85, 1.0), .size = 13_px }
    };

    Style ButtonPrimary = {
        .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
        .padding = { .left = 12_px, .right = 12_px, .top = 6_px, .bottom = 6_px },
        .background = { .color = rgba(79, 99, 255, 1.0), .transition = 120_ms },
        .border = {
            .color = rgba(79, 99, 255, 1.0),
            .radius = 3_px,
            .width = 1_px
        },
        .cursor = Cursor::Hand
    };

    Style ButtonPrimaryHover = {
        .applies = { .hover = true, .focus = true },
        .background = { .color = rgba(96, 117, 255, 1.0) }
    };

    Style ButtonPrimaryLabel = {
        .text = { .color = rgba(255, 255, 255, 0.98), .size = 13_px }
    };

    Style CheckboxBox = {
        .layout = { Axis::Horizontal, Align::Center, Align::Center },
        .size = { 20_px, 20_px },
        .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .radius = 6_px,
            .width = 1_px
        },
        .shadow = subtleShadow,
        .cursor = Cursor::Hand
    };

    Style CheckboxDisabled = {
        .applies = { .disabled = true },
        .cursor = Cursor::Default
    };

    Style CheckboxFocus = {
        .applies = { .focus = true },
        .border = { .color = rgba(79, 99, 255, 1.0), .width = 1_px }
    };

    Style CheckboxChecked = {
        .background = { .color = rgba(79, 99, 255, 1.0) },
        .border = { .color = rgba(79, 99, 255, 1.0) }
    };

    Style CheckboxPress = {
        .applies = { .press = true },
        .background = { .color = rgba(67, 85, 220, 1.0) }
    };

    Style CheckboxMark = {
        .size = { 14_px, 14_px },
        .text = { .color = rgba(255, 255, 255, 1.0) }
    };

    Style SliderTrack = {
        .layout = { Axis::Horizontal, Align::Start, Align::Center },
        .size = { .width = 100_pct, .height = 2_px },
        .background = { .color = rgba(203, 213, 225, 1.0) }
    };

    Style SliderThumb = {
        .size = { .width = 4_px, .height = 14_px },
        .background = { .color = rgba(79, 99, 255, 1.0) },
        .border = { .radius = 2_px }
    };

    Style SliderThumbHover = {
        .applies = { .hover = true, .drag = true },
        .size = { .width = 6_px, .height = 16_px }
    };

    Style SliderValueText = {
        .text = { .color = rgba(30, 41, 59, 1.0), .size = 12_px }
    };

    Style SliderFieldHover = {
        .applies = { .hover = true, .drag = true },
        .border = { .color = rgba(79, 99, 255, 0.35), .width = 1_px }
    };

    struct Palette {

        sColor fieldSurface;
        sColor fieldDisabledSurface;   // recessed surface for a locked/disabled field
        sColor dropdownSurface;        // closed dropdown box (overrides fieldSurface)
        sColor optionsSurface;         // opaque dropdown popup surface
        sColor fieldBorder;
        sColor fieldText;
        sColor labelText;
        sColor placeholderText;
        sColor focusBorder;
        sColor dropdownArrow;
        sColor optionHover;
        sColor optionSelected;
        sColor optionDisabledText;
        sColor sliderTrack;
        sColor sliderThumb;
        sColor sliderHoverBorder;
        sColor sliderValueText;
        sColor checkboxSurface;
        sColor checkboxBorder;
        sColor checkboxChecked;
        sColor checkboxPress;
        sColor checkboxMark;
        sColor buttonSecondarySurface;
        sColor buttonSecondaryBorder;
        sColor buttonSecondaryHover;
        sColor buttonSecondaryLabel;
        sColor buttonPrimarySurface;
        sColor buttonPrimaryHover;
        sColor buttonPrimaryLabel;
        sColor shadowColor;
    };

    inline void markDirty(Style& style) {
        style.dirty = true;
    }

    inline void applyFieldShadow(Shadow& shadow, sColor color) {
        shadow.color = color;
        shadow.size = Px(-2);
        shadow.blur = 8_px;
        shadow.y = 2_px;
        shadow.x = 0_px;
    }

    inline void applyFieldShadow(Style& style, sColor color) {
        applyFieldShadow(style.shadow, color);
    }

    inline void applyPalette(const Palette& colors) {

        Label.text.color = colors.labelText;

        Field.background.color = colors.fieldSurface;
        Field.border.color = colors.fieldBorder;
        FieldFocus.border.color = colors.focusBorder;
        DropdownSurface.background.color = colors.dropdownSurface;

        FieldText.text.color = colors.fieldText;
        FieldDisabled.background.color = colors.fieldDisabledSurface;
        FieldDisabled.border.color = colors.fieldBorder;
        FieldTextDisabled.text.color = colors.optionDisabledText;
        FieldSuffix.text.color = colors.labelText;
        FieldSuffix.text.color.a = 0.72f;
        FieldSuffixDisabled.text.color = colors.optionDisabledText;
        FieldSuffixDisabled.text.color.a = 0.82f;
        Placeholder.text.color = colors.placeholderText;
        DropdownArrow.text.color = colors.dropdownArrow;

        OptionsContainer.background.color = colors.optionsSurface;
        OptionsContainer.border.color = colors.fieldBorder;

        OptionsContainerUpward.background.color = colors.optionsSurface;
        OptionsContainerUpward.border.color = colors.fieldBorder;

        Option.background.color = colors.optionsSurface;
        Option.background.color.a = 0.0f;
        Option.text.color = colors.fieldText;
        OptionHover.background.color = colors.optionHover;
        OptionSelected.background.color = colors.optionSelected;
        OptionMenuHighlight.background.color = colors.optionHover;
        OptionDisabled.text.color = colors.optionDisabledText;

        ButtonSecondary.background.color = colors.buttonSecondarySurface;
        ButtonSecondary.border.color = colors.buttonSecondaryBorder;
        ButtonSecondaryHover.background.color = colors.buttonSecondaryHover;
        ButtonSecondaryHover.border.color = colors.fieldBorder;
        ButtonSecondaryLabel.text.color = colors.buttonSecondaryLabel;

        ButtonPrimary.background.color = colors.buttonPrimarySurface;
        ButtonPrimaryHover.background.color = colors.buttonPrimaryHover;
        ButtonPrimaryLabel.text.color = colors.buttonPrimaryLabel;

        CheckboxBox.background.color = colors.checkboxSurface;
        CheckboxBox.border.color = colors.checkboxBorder;
        CheckboxFocus.border.color = colors.focusBorder;
        CheckboxChecked.background.color = colors.checkboxChecked;
        CheckboxChecked.border.color = colors.checkboxChecked;
        CheckboxPress.background.color = colors.checkboxPress;
        CheckboxMark.text.color = colors.checkboxMark;

        SliderTrack.background.color = colors.sliderTrack;
        SliderThumb.background.color = colors.sliderThumb;
        SliderValueText.text.color = colors.sliderValueText;
        SliderFieldHover.border.color = colors.sliderHoverBorder;

        applyFieldShadow(subtleShadow, colors.shadowColor);
        applyFieldShadow(Field, colors.shadowColor);
        applyFieldShadow(OptionsContainer, colors.shadowColor);
        applyFieldShadow(OptionsContainerUpward, colors.shadowColor);
        applyFieldShadow(CheckboxBox, colors.shadowColor);
        applyFieldShadow(ButtonSecondary, colors.shadowColor);

        markDirty(Label);
        markDirty(Field);
        markDirty(FieldFocus);
        markDirty(DropdownSurface);
        markDirty(FieldDisabled);
        markDirty(FieldText);
        markDirty(FieldTextDisabled);
        markDirty(FieldSuffix);
        markDirty(FieldSuffixDisabled);
        markDirty(Placeholder);
        markDirty(DropdownArrow);
        markDirty(OptionsContainer);
        markDirty(OptionsContainerUpward);
        markDirty(Option);
        markDirty(OptionHover);
        markDirty(OptionSelected);
        markDirty(OptionMenuHighlight);
        markDirty(OptionDisabled);
        markDirty(ButtonSecondary);
        markDirty(ButtonSecondaryHover);
        markDirty(ButtonSecondaryLabel);
        markDirty(ButtonPrimary);
        markDirty(ButtonPrimaryHover);
        markDirty(ButtonPrimaryLabel);
        markDirty(CheckboxBox);
        markDirty(CheckboxDisabled);
        markDirty(CheckboxFocus);
        markDirty(CheckboxChecked);
        markDirty(CheckboxPress);
        markDirty(CheckboxMark);
        markDirty(SliderTrack);
        markDirty(SliderThumb);
        markDirty(SliderValueText);
        markDirty(SliderFieldHover);
    }
}
