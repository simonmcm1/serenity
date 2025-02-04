/*
 * Copyright (c) 2022, Matthew Olsson <mattco@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibPDF/CommonNames.h>
#include <LibPDF/Fonts/Type0Font.h>

namespace PDF {

class CIDFontType {
public:
    virtual ~CIDFontType() = default;
    virtual PDFErrorOr<Gfx::FloatPoint> draw_string(Gfx::Painter&, Gfx::FloatPoint, DeprecatedString const&, Color const&, float, float, float, float) = 0;
};

class CIDFontType0 : public CIDFontType {
public:
    PDFErrorOr<Gfx::FloatPoint> draw_string(Gfx::Painter&, Gfx::FloatPoint, DeprecatedString const&, Color const&, float, float, float, float) override;
};

PDFErrorOr<Gfx::FloatPoint> CIDFontType0::draw_string(Gfx::Painter&, Gfx::FloatPoint, DeprecatedString const&, Color const&, float, float, float, float)
{
    // ISO 32000 (PDF 2.0) 9.7.4.2 Glyph selection in CIDFonts
    // "When the CIDFont contains an embedded font program that is represented in the Compact Font Format (CFF),
    //  the FontFile3 entry in the font descriptor (...) shall be either CIDFontType0C or OpenType.
    //  There are two cases, depending on the contents of the font program:
    //  * The "CFF" font program has a Top DICT that uses CIDFont operators: The CIDs shall be used to determine
    //    the GID value for the glyph procedure using the charset table in the CFF program.
    //    The GID value shall then be used to look up the glyph procedure using the CharStrings INDEX table [...]
    //  * The "CFF" font program has a Top DICT that does not use CIDFont operators: The CIDs shall be used
    //    directly as GID values, and the glyph procedure shall be retrieved using the CharStrings INDEX"
    return Error::rendering_unsupported_error("Type0 font CIDFontType0 not implemented yet");
}

class CIDFontType2 : public CIDFontType {
public:
    PDFErrorOr<Gfx::FloatPoint> draw_string(Gfx::Painter&, Gfx::FloatPoint, DeprecatedString const&, Color const&, float, float, float, float) override;
};

PDFErrorOr<Gfx::FloatPoint> CIDFontType2::draw_string(Gfx::Painter&, Gfx::FloatPoint, DeprecatedString const&, Color const&, float, float, float, float)
{
    // ISO 32000 (PDF 2.0) 9.7.4.2 Glyph selection in CIDFonts
    // "For Type 2, the CIDFont program is actually a TrueType font program, which has no native notion of CIDs.
    //  In a TrueType font program, glyph descriptions are identified by glyph index values.
    //  Glyph indices are internal to the font and are not defined consistently from one font to another.
    //  Instead, a TrueType font program contains a "cmap" table that provides mappings directly from
    //  character codes to glyph indices for one or more predefined encodings.
    //  TrueType font programs are integrated with the CID-keyed font architecture in one of two ways,
    //  depending on whether the font program is embedded in the PDF file:
    //  * If the TrueType font program is embedded, the Type 2 CIDFont dictionary shall contain a CIDToGIDMap entry
    //    that maps CIDs to the glyph indices for the appropriate glyph descriptions in that font program.
    //  * If the TrueType font program is not embedded but is referenced by name, and the Type 2 CIDFont dictionary
    //    contains a CIDToGIDMap entry, the CIDToGIDMap entry shall be ignored, since it is not meaningful
    ///   to refer to glyph indices in an external font program."
    return Error::rendering_unsupported_error("Type0 font CIDFontType2 not implemented yet");
}

Type0Font::Type0Font() = default;
Type0Font::~Type0Font() = default;

PDFErrorOr<void> Type0Font::initialize(Document* document, NonnullRefPtr<DictObject> const& dict, float font_size)
{
    TRY(PDFFont::initialize(document, dict, font_size));

    // FIXME: Support arbitrary CMaps
    auto cmap_value = TRY(dict->get_object(document, CommonNames::Encoding));
    if (!cmap_value->is<NameObject>() || cmap_value->cast<NameObject>()->name() != CommonNames::IdentityH)
        TODO();

    auto descendant_font_value = TRY(dict->get_array(document, CommonNames::DescendantFonts));
    auto descendant_font = TRY(descendant_font_value->get_dict_at(document, 0));

    auto system_info_dict = TRY(descendant_font->get_dict(document, CommonNames::CIDSystemInfo));
    auto registry = TRY(system_info_dict->get_string(document, CommonNames::Registry))->string();
    auto ordering = TRY(system_info_dict->get_string(document, CommonNames::Ordering))->string();
    u8 supplement = system_info_dict->get_value(CommonNames::Supplement).get<int>();
    CIDSystemInfo system_info { registry, ordering, supplement };

    auto subtype = TRY(descendant_font->get_name(document, CommonNames::Subtype))->name();
    if (subtype == CommonNames::CIDFontType0) {
        // CFF-based
        m_cid_font_type = TRY(try_make<CIDFontType0>());
    } else if (subtype == CommonNames::CIDFontType2) {
        // TrueType-based
        m_cid_font_type = TRY(try_make<CIDFontType2>());
    } else {
        return Error { Error::Type::MalformedPDF, "invalid /Subtype for Type 0 font" };
    }

    auto font_descriptor = TRY(descendant_font->get_dict(document, CommonNames::FontDescriptor));

    u16 default_width = 1000;
    if (descendant_font->contains(CommonNames::DW))
        default_width = descendant_font->get_value(CommonNames::DW).to_int();

    HashMap<u16, u16> widths;

    if (descendant_font->contains(CommonNames::W)) {
        auto widths_array = MUST(descendant_font->get_array(document, CommonNames::W));
        Optional<u16> pending_code;

        for (size_t i = 0; i < widths_array->size(); i++) {
            auto& value = widths_array->at(i);
            if (!pending_code.has_value()) {
                pending_code = value.to_int();
            } else if (value.has<NonnullRefPtr<Object>>()) {
                auto array = value.get<NonnullRefPtr<Object>>()->cast<ArrayObject>();
                auto code = pending_code.release_value();
                for (auto& width : *array)
                    widths.set(code++, width.to_int());
            } else {
                auto first_code = pending_code.release_value();
                auto last_code = value.to_int();
                auto width = widths_array->at(i + 1).to_int();
                for (u16 code = first_code; code <= last_code; code++)
                    widths.set(code, width);

                i++;
            }
        }
    }

    if (dict->contains(CommonNames::CIDToGIDMap)) {
        auto value = TRY(dict->get_object(document, CommonNames::CIDToGIDMap));
        if (value->is<StreamObject>()) {
            TODO();
        } else if (value->cast<NameObject>()->name() != "Identity") {
            TODO();
        }
    }

    m_system_info = move(system_info);
    m_widths = move(widths);
    m_missing_width = default_width;
    return {};
}

float Type0Font::get_char_width(u16 char_code) const
{
    u16 width;
    if (auto char_code_width = m_widths.get(char_code); char_code_width.has_value()) {
        width = char_code_width.value();
    } else {
        width = m_missing_width;
    }

    return static_cast<float>(width) / 1000.0f;
}

void Type0Font::set_font_size(float)
{
}

PDFErrorOr<Gfx::FloatPoint> Type0Font::draw_string(Gfx::Painter& painter, Gfx::FloatPoint glyph_position, DeprecatedString const& string, Color const& paint_color, float font_size, float character_spacing, float word_spacing, float horizontal_scaling)
{
    return m_cid_font_type->draw_string(painter, glyph_position, string, paint_color, font_size, character_spacing, word_spacing, horizontal_scaling);
}

}
