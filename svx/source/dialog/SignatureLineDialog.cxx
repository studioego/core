/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <svx/SignatureLineDialog.hxx>

#include <comphelper/processfactory.hxx>
#include <comphelper/xmltools.hxx>
#include <tools/stream.hxx>
#include <unotools/streamwrap.hxx>
#include <vcl/weld.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/graphic/GraphicProvider.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/graphic/XGraphicProvider.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/text/TextContentAnchorType.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextDocument.hpp>

using namespace css;
using namespace css::uno;
using namespace css::beans;
using namespace css::frame;
using namespace css::io;
using namespace css::lang;
using namespace css::frame;
using namespace css::text;
using namespace css::drawing;
using namespace css::graphic;

SignatureLineDialog::SignatureLineDialog(weld::Widget* pParent, Reference<XModel> xModel,
                                         bool bEditExisting)
    : GenericDialogController(pParent, "svx/ui/signatureline.ui", "SignatureLineDialog")
    , m_xEditName(m_xBuilder->weld_entry("edit_name"))
    , m_xEditTitle(m_xBuilder->weld_entry("edit_title"))
    , m_xEditEmail(m_xBuilder->weld_entry("edit_email"))
    , m_xEditInstructions(m_xBuilder->weld_text_view("edit_instructions"))
    , m_xCheckboxCanAddComments(m_xBuilder->weld_check_button("checkbox_can_add_comments"))
    , m_xCheckboxShowSignDate(m_xBuilder->weld_check_button("checkbox_show_sign_date"))
    , m_xModel(xModel)
{
    m_xEditInstructions->set_size_request(m_xEditInstructions->get_approximate_digit_width() * 48,
                                          m_xEditInstructions->get_text_height() * 5);

    // No signature line selected - start with empty dialog and generate a new one
    if (!bEditExisting)
        return;

    Reference<container::XIndexAccess> xIndexAccess(m_xModel->getCurrentSelection(),
                                                    UNO_QUERY_THROW);
    Reference<XPropertySet> xProps(xIndexAccess->getByIndex(0), UNO_QUERY_THROW);

    // Read properties from selected signature line
    xProps->getPropertyValue("SignatureLineId") >>= m_aSignatureLineId;
    OUString aSuggestedSignerName;
    xProps->getPropertyValue("SignatureLineSuggestedSignerName") >>= aSuggestedSignerName;
    m_xEditName->set_text(aSuggestedSignerName);
    OUString aSuggestedSignerTitle;
    xProps->getPropertyValue("SignatureLineSuggestedSignerTitle") >>= aSuggestedSignerTitle;
    m_xEditTitle->set_text(aSuggestedSignerTitle);
    OUString aSuggestedSignerEmail;
    xProps->getPropertyValue("SignatureLineSuggestedSignerEmail") >>= aSuggestedSignerEmail;
    m_xEditEmail->set_text(aSuggestedSignerEmail);
    OUString aSigningInstructions;
    xProps->getPropertyValue("SignatureLineSigningInstructions") >>= aSigningInstructions;
    m_xEditInstructions->set_text(aSigningInstructions);
    bool bCanAddComments = false;
    xProps->getPropertyValue("SignatureLineCanAddComment") >>= bCanAddComments;
    m_xCheckboxCanAddComments->set_active(bCanAddComments);
    bool bShowSignDate = false;
    xProps->getPropertyValue("SignatureLineShowSignDate") >>= bShowSignDate;
    m_xCheckboxShowSignDate->set_active(bShowSignDate);

    // Mark this as existing shape
    m_xExistingShapeProperties = xProps;
}

short SignatureLineDialog::execute()
{
    short nRet = run();
    if (nRet == RET_OK)
        Apply();
    return nRet;
}

void SignatureLineDialog::Apply()
{
    if (m_aSignatureLineId.isEmpty())
        m_aSignatureLineId
            = OStringToOUString(comphelper::xml::generateGUIDString(), RTL_TEXTENCODING_ASCII_US);
    OUString aSignerName(m_xEditName->get_text());
    OUString aSignerTitle(m_xEditTitle->get_text());
    OUString aSignerEmail(m_xEditEmail->get_text());
    OUString aSigningInstructions(m_xEditInstructions->get_text());
    bool bCanAddComments(m_xCheckboxCanAddComments->get_active());
    bool bShowSignDate(m_xCheckboxShowSignDate->get_active());

    // Read svg and replace placeholder texts
    OUString aSvgImage(getSignatureImage());
    aSvgImage = aSvgImage.replaceAll("[SIGNER_NAME]", aSignerName);
    aSvgImage = aSvgImage.replaceAll("[SIGNER_TITLE]", aSignerTitle);

    // These are only filled if the signature line is signed.
    aSvgImage = aSvgImage.replaceAll("[SIGNATURE]", "");
    aSvgImage = aSvgImage.replaceAll("[SIGNED_BY]", "");
    aSvgImage = aSvgImage.replaceAll("[INVALID_SIGNATURE]", "");
    aSvgImage = aSvgImage.replaceAll("[DATE]", "");

    // Insert/Update graphic
    SvMemoryStream aSvgStream(4096, 4096);
    aSvgStream.WriteOString(OUStringToOString(aSvgImage, RTL_TEXTENCODING_UTF8));
    Reference<XInputStream> xInputStream(new utl::OSeekableInputStreamWrapper(aSvgStream));
    Reference<XComponentContext> xContext(comphelper::getProcessComponentContext());
    Reference<XGraphicProvider> xProvider = css::graphic::GraphicProvider::create(xContext);

    Sequence<PropertyValue> aMediaProperties(1);
    aMediaProperties[0].Name = "InputStream";
    aMediaProperties[0].Value <<= xInputStream;
    Reference<XGraphic> xGraphic(xProvider->queryGraphic(aMediaProperties));

    bool bIsExistingSignatureLine = m_xExistingShapeProperties.is();
    Reference<XPropertySet> xShapeProps;
    if (bIsExistingSignatureLine)
        xShapeProps = m_xExistingShapeProperties;
    else
        xShapeProps.set(Reference<lang::XMultiServiceFactory>(m_xModel, UNO_QUERY)
                            ->createInstance("com.sun.star.drawing.GraphicObjectShape"),
                        UNO_QUERY);

    xShapeProps->setPropertyValue("Graphic", Any(xGraphic));

    // Set signature line properties
    xShapeProps->setPropertyValue("IsSignatureLine", Any(true));
    xShapeProps->setPropertyValue("SignatureLineId", Any(m_aSignatureLineId));
    if (!aSignerName.isEmpty())
        xShapeProps->setPropertyValue("SignatureLineSuggestedSignerName", Any(aSignerName));
    if (!aSignerTitle.isEmpty())
        xShapeProps->setPropertyValue("SignatureLineSuggestedSignerTitle", Any(aSignerTitle));
    if (!aSignerEmail.isEmpty())
        xShapeProps->setPropertyValue("SignatureLineSuggestedSignerEmail", Any(aSignerEmail));
    if (!aSigningInstructions.isEmpty())
        xShapeProps->setPropertyValue("SignatureLineSigningInstructions",
                                      Any(aSigningInstructions));
    xShapeProps->setPropertyValue("SignatureLineShowSignDate", Any(bShowSignDate));
    xShapeProps->setPropertyValue("SignatureLineCanAddComment", Any(bCanAddComments));

    if (!bIsExistingSignatureLine)
    {
        // Default size
        Reference<XShape> xShape(xShapeProps, UNO_QUERY);
        awt::Size aShapeSize;
        aShapeSize.Height = 3000;
        aShapeSize.Width = 6000;
        xShape->setSize(aShapeSize);

        // Default anchoring
        xShapeProps->setPropertyValue("AnchorType", Any(TextContentAnchorType_AT_PARAGRAPH));

        // Insert into document
        Reference<XTextRange> const xEnd
            = Reference<XTextDocument>(m_xModel, UNO_QUERY)->getText()->getEnd();
        Reference<XTextContent> const xShapeContent(xShapeProps, UNO_QUERY);
        xShapeContent->attach(xEnd);
    }
}

OUString SignatureLineDialog::getSignatureImage()
{
    OUString const svg(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?><svg "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:cc=\"http://creativecommons.org/ns#\" "
        "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
        "xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns=\"http://www.w3.org/2000/svg\" "
        "xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\" "
        "xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" version=\"1.2\" "
        "width=\"90mm\" height=\"45mm\" viewBox=\"0 0 9000 4500\" preserveAspectRatio=\"xMidYMid\" "
        "fill-rule=\"evenodd\" stroke-width=\"28.222\" stroke-linejoin=\"round\" "
        "xml:space=\"preserve\" id=\"svg577\" inkscape:version=\"0.92.2 (5c3e80d, "
        "2017-08-06)\"><metadata id=\"metadata581\"><rdf:RDF><cc:Work "
        "rdf:about=\"\"><dc:format>image/svg+xml</dc:format><dc:type "
        "rdf:resource=\"http://purl.org/dc/dcmitype/StillImage\"/><dc:title/></cc:Work></rdf:RDF></"
        "metadata><sodipodi:namedview pagecolor=\"#ffffff\" bordercolor=\"#666666\" "
        "borderopacity=\"1\" objecttolerance=\"10\" gridtolerance=\"10\" guidetolerance=\"10\" "
        "inkscape:pageopacity=\"0\" inkscape:pageshadow=\"2\" inkscape:window-width=\"1863\" "
        "inkscape:window-height=\"1056\" id=\"namedview579\" showgrid=\"false\" "
        "inkscape:zoom=\"0.90252315\" inkscape:cx=\"170.07874\" inkscape:cy=\"85.03937\" "
        "inkscape:window-x=\"57\" inkscape:window-y=\"24\" inkscape:window-maximized=\"1\" "
        "inkscape:current-layer=\"svg577\" inkscape:pagecheckerboard=\"false\"/><defs "
        "class=\"ClipPathGroup\" id=\"defs8\"><clipPath id=\"presentation_clip_path\" "
        "clipPathUnits=\"userSpaceOnUse\"><rect x=\"0\" y=\"0\" width=\"9000\" height=\"4500\" "
        "id=\"rect2\"/></clipPath></defs><defs id=\"defs49\"/><defs id=\"defs86\"/><defs "
        "class=\"TextShapeIndex\" id=\"defs90\"/><defs class=\"EmbeddedBulletChars\" "
        "id=\"defs122\"/><defs class=\"TextEmbeddedBitmaps\" id=\"defs124\"/><g id=\"g129\"><g "
        "id=\"id2\" class=\"Master_Slide\"><g id=\"bg-id2\" class=\"Background\"/><g id=\"bo-id2\" "
        "class=\"BackgroundObjects\"/></g></g><g class=\"SlideGroup\" id=\"g575\"><g "
        "id=\"g573\"><g id=\"container-id1\"><g id=\"id1\" class=\"Slide\" "
        "clip-path=\"url(#presentation_clip_path)\"><g class=\"Page\" id=\"g569\"><g "
        "class=\"com.sun.star.drawing.LineShape\" id=\"g154\"><g id=\"id3\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"-27\" y=\"2373\" width=\"9055\" "
        "height=\"55\" id=\"rect131\"/><desc id=\"desc133\">150</desc><desc "
        "id=\"desc135\">139</desc><desc id=\"desc137\">132</desc><desc id=\"desc139\">512: "
        "XPATHSTROKE_SEQ_BEGIN</desc><desc id=\"desc141\">132</desc><desc "
        "id=\"desc143\">133</desc><desc id=\"desc145\">109</desc><path fill=\"none\" "
        "stroke=\"rgb(0,0,0)\" stroke-width=\"53\" stroke-linejoin=\"round\" d=\"M 0,2400 L "
        "9000,2400\" id=\"path147\"/><desc id=\"desc149\">512: XPATHSTROKE_SEQ_END</desc><desc "
        "id=\"desc151\">140</desc></g></g><g class=\"com.sun.star.drawing.ClosedBezierShape\" "
        "id=\"g173\"><g id=\"id4\"><rect class=\"BoundingBox\" stroke=\"none\" fill=\"none\" "
        "x=\"301\" y=\"1400\" width=\"801\" height=\"801\" id=\"rect156\"/><desc "
        "id=\"desc158\">150</desc><desc id=\"desc160\">139</desc><desc "
        "id=\"desc162\">133</desc><desc id=\"desc164\">132</desc><desc "
        "id=\"desc166\">111</desc><path fill=\"rgb(0,0,0)\" stroke=\"none\" d=\"M 969,2200 C "
        "880,2083 792,1967 704,1850 614,1967 523,2083 433,2200 389,2200 345,2200 301,2200 413,2061 "
        "525,1923 637,1784 533,1656 430,1528 327,1400 371,1400 415,1400 459,1400 541,1505 623,1609 "
        "704,1714 784,1609 863,1505 943,1400 987,1400 1031,1400 1075,1400 975,1527 874,1653 "
        "773,1780 882,1920 992,2060 1101,2200 1057,2200 1013,2200 969,2200 Z\" "
        "id=\"path168\"/><desc id=\"desc170\">140</desc></g></g><g "
        "class=\"com.sun.star.drawing.TextShape\" id=\"g236\"><g id=\"id5\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"1300\" y=\"1500\" width=\"8001\" "
        "height=\"925\" id=\"rect175\"/><desc id=\"desc177\">150</desc><desc id=\"desc179\">512: "
        "XTEXT_PAINTSHAPE_BEGIN</desc><text class=\"TextShape\" id=\"text233\"><desc "
        "class=\"Paragraph\" id=\"desc181\"/><tspan class=\"TextParagraph\" "
        "font-family=\"Liberation Sans, sans-serif\" font-size=\"600px\" font-weight=\"400\" "
        "id=\"tspan231\"><desc id=\"desc183\">138</desc><desc id=\"desc185\">136</desc><desc "
        "id=\"desc187\">135</desc><desc id=\"desc189\">134</desc><desc "
        "id=\"desc191\">113</desc><desc class=\"TextPortion\" id=\"desc193\">type: Text; content: "
        "[SIGNATURE]; </desc><tspan class=\"TextPosition\" x=\"1550\" y=\"2171\" "
        "id=\"tspan229\"><tspan fill=\"rgb(0,0,0)\" stroke=\"none\" "
        "id=\"tspan195\">[SIGNATURE]</tspan><desc id=\"desc197\">512: XTEXT_EOC</desc><desc "
        "id=\"desc199\">512: XTEXT_EOC</desc><desc id=\"desc201\">512: XTEXT_EOW</desc><desc "
        "id=\"desc203\">512: XTEXT_EOC</desc><desc id=\"desc205\">512: XTEXT_EOC</desc><desc "
        "id=\"desc207\">512: XTEXT_EOC</desc><desc id=\"desc209\">512: XTEXT_EOC</desc><desc "
        "id=\"desc211\">512: XTEXT_EOC</desc><desc id=\"desc213\">512: XTEXT_EOC</desc><desc "
        "id=\"desc215\">512: XTEXT_EOC</desc><desc id=\"desc217\">512: XTEXT_EOC</desc><desc "
        "id=\"desc219\">512: XTEXT_EOC</desc><desc id=\"desc221\">512: XTEXT_EOW</desc><desc "
        "id=\"desc223\">512: XTEXT_EOL</desc><desc id=\"desc225\">512: XTEXT_EOP</desc><desc "
        "id=\"desc227\">512: XTEXT_PAINTSHAPE_END</desc></tspan></tspan></text></g></g><g "
        "class=\"com.sun.star.drawing.TextShape\" id=\"g303\"><g id=\"id6\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"100\" y=\"2500\" width=\"8901\" "
        "height=\"726\" id=\"rect238\"/><desc id=\"desc240\">150</desc><desc id=\"desc242\">512: "
        "XTEXT_PAINTSHAPE_BEGIN</desc><text class=\"TextShape\" id=\"text300\"><desc "
        "class=\"Paragraph\" id=\"desc244\"/><tspan class=\"TextParagraph\" "
        "font-family=\"Liberation Sans, sans-serif\" font-size=\"423px\" font-weight=\"400\" "
        "id=\"tspan298\"><desc id=\"desc246\">138</desc><desc id=\"desc248\">136</desc><desc "
        "id=\"desc250\">135</desc><desc id=\"desc252\">134</desc><desc "
        "id=\"desc254\">113</desc><desc class=\"TextPortion\" id=\"desc256\">type: Text; content: "
        "[SIGNER_NAME]; </desc><tspan class=\"TextPosition\" x=\"350\" y=\"3010\" "
        "id=\"tspan296\"><tspan fill=\"rgb(0,0,0)\" stroke=\"none\" "
        "id=\"tspan258\">[SIGNER_NAME]</tspan><desc id=\"desc260\">512: XTEXT_EOC</desc><desc "
        "id=\"desc262\">512: XTEXT_EOC</desc><desc id=\"desc264\">512: XTEXT_EOW</desc><desc "
        "id=\"desc266\">512: XTEXT_EOC</desc><desc id=\"desc268\">512: XTEXT_EOC</desc><desc "
        "id=\"desc270\">512: XTEXT_EOC</desc><desc id=\"desc272\">512: XTEXT_EOC</desc><desc "
        "id=\"desc274\">512: XTEXT_EOC</desc><desc id=\"desc276\">512: XTEXT_EOC</desc><desc "
        "id=\"desc278\">512: XTEXT_EOC</desc><desc id=\"desc280\">512: XTEXT_EOC</desc><desc "
        "id=\"desc282\">512: XTEXT_EOC</desc><desc id=\"desc284\">512: XTEXT_EOC</desc><desc "
        "id=\"desc286\">512: XTEXT_EOC</desc><desc id=\"desc288\">512: XTEXT_EOW</desc><desc "
        "id=\"desc290\">512: XTEXT_EOL</desc><desc id=\"desc292\">512: XTEXT_EOP</desc><desc "
        "id=\"desc294\">512: XTEXT_PAINTSHAPE_END</desc></tspan></tspan></text></g></g><g "
        "class=\"com.sun.star.drawing.TextShape\" id=\"g372\"><g id=\"id7\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"100\" y=\"3075\" width=\"8901\" "
        "height=\"726\" id=\"rect305\"/><desc id=\"desc307\">150</desc><desc id=\"desc309\">512: "
        "XTEXT_PAINTSHAPE_BEGIN</desc><text class=\"TextShape\" id=\"text369\"><desc "
        "class=\"Paragraph\" id=\"desc311\"/><tspan class=\"TextParagraph\" "
        "font-family=\"Liberation Sans, sans-serif\" font-size=\"423px\" font-weight=\"400\" "
        "id=\"tspan367\"><desc id=\"desc313\">138</desc><desc id=\"desc315\">136</desc><desc "
        "id=\"desc317\">135</desc><desc id=\"desc319\">134</desc><desc "
        "id=\"desc321\">113</desc><desc class=\"TextPortion\" id=\"desc323\">type: Text; content: "
        "[SIGNER_TITLE]; </desc><tspan class=\"TextPosition\" x=\"350\" y=\"3585\" "
        "id=\"tspan365\"><tspan fill=\"rgb(0,0,0)\" stroke=\"none\" "
        "id=\"tspan325\">[SIGNER_TITLE]</tspan><desc id=\"desc327\">512: XTEXT_EOC</desc><desc "
        "id=\"desc329\">512: XTEXT_EOC</desc><desc id=\"desc331\">512: XTEXT_EOW</desc><desc "
        "id=\"desc333\">512: XTEXT_EOC</desc><desc id=\"desc335\">512: XTEXT_EOC</desc><desc "
        "id=\"desc337\">512: XTEXT_EOC</desc><desc id=\"desc339\">512: XTEXT_EOC</desc><desc "
        "id=\"desc341\">512: XTEXT_EOC</desc><desc id=\"desc343\">512: XTEXT_EOC</desc><desc "
        "id=\"desc345\">512: XTEXT_EOC</desc><desc id=\"desc347\">512: XTEXT_EOC</desc><desc "
        "id=\"desc349\">512: XTEXT_EOC</desc><desc id=\"desc351\">512: XTEXT_EOC</desc><desc "
        "id=\"desc353\">512: XTEXT_EOC</desc><desc id=\"desc355\">512: XTEXT_EOC</desc><desc "
        "id=\"desc357\">512: XTEXT_EOW</desc><desc id=\"desc359\">512: XTEXT_EOL</desc><desc "
        "id=\"desc361\">512: XTEXT_EOP</desc><desc id=\"desc363\">512: "
        "XTEXT_PAINTSHAPE_END</desc></tspan></tspan></text></g></g><g "
        "class=\"com.sun.star.drawing.TextShape\" id=\"g435\"><g id=\"id8\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"100\" y=\"3660\" width=\"8901\" "
        "height=\"726\" id=\"rect374\"/><desc id=\"desc376\">150</desc><desc id=\"desc378\">512: "
        "XTEXT_PAINTSHAPE_BEGIN</desc><text class=\"TextShape\" id=\"text432\"><desc "
        "class=\"Paragraph\" id=\"desc380\"/><tspan class=\"TextParagraph\" "
        "font-family=\"Liberation Sans, sans-serif\" font-size=\"423px\" font-weight=\"400\" "
        "id=\"tspan430\"><desc id=\"desc382\">138</desc><desc id=\"desc384\">136</desc><desc "
        "id=\"desc386\">135</desc><desc id=\"desc388\">134</desc><desc "
        "id=\"desc390\">113</desc><desc class=\"TextPortion\" id=\"desc392\">type: Text; content: "
        "[SIGNED_BY]; </desc><tspan class=\"TextPosition\" x=\"350\" y=\"4170\" "
        "id=\"tspan428\"><tspan fill=\"rgb(0,0,0)\" stroke=\"none\" "
        "id=\"tspan394\">[SIGNED_BY]</tspan><desc id=\"desc396\">512: XTEXT_EOC</desc><desc "
        "id=\"desc398\">512: XTEXT_EOC</desc><desc id=\"desc400\">512: XTEXT_EOW</desc><desc "
        "id=\"desc402\">512: XTEXT_EOC</desc><desc id=\"desc404\">512: XTEXT_EOC</desc><desc "
        "id=\"desc406\">512: XTEXT_EOC</desc><desc id=\"desc408\">512: XTEXT_EOC</desc><desc "
        "id=\"desc410\">512: XTEXT_EOC</desc><desc id=\"desc412\">512: XTEXT_EOC</desc><desc "
        "id=\"desc414\">512: XTEXT_EOC</desc><desc id=\"desc416\">512: XTEXT_EOC</desc><desc "
        "id=\"desc418\">512: XTEXT_EOC</desc><desc id=\"desc420\">512: XTEXT_EOW</desc><desc "
        "id=\"desc422\">512: XTEXT_EOL</desc><desc id=\"desc424\">512: XTEXT_EOP</desc><desc "
        "id=\"desc426\">512: XTEXT_PAINTSHAPE_END</desc></tspan></tspan></text></g></g><g "
        "class=\"com.sun.star.drawing.TextShape\" id=\"g488\"><g id=\"id9\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"4800\" y=\"0\" width=\"4201\" "
        "height=\"726\" id=\"rect437\"/><desc id=\"desc439\">150</desc><desc id=\"desc441\">512: "
        "XTEXT_PAINTSHAPE_BEGIN</desc><text class=\"TextShape\" id=\"text485\"><desc "
        "class=\"Paragraph\" id=\"desc443\"/><tspan class=\"TextParagraph\" "
        "font-family=\"Liberation Sans, sans-serif\" font-size=\"423px\" font-weight=\"400\" "
        "id=\"tspan483\"><desc id=\"desc445\">138</desc><desc id=\"desc447\">136</desc><desc "
        "id=\"desc449\">135</desc><desc id=\"desc451\">134</desc><desc "
        "id=\"desc453\">113</desc><desc class=\"TextPortion\" id=\"desc455\">type: Text; content: "
        "[DATE]; </desc><tspan class=\"TextPosition\" x=\"7417\" y=\"510\" id=\"tspan481\"><tspan "
        "fill=\"rgb(0,0,0)\" stroke=\"none\" id=\"tspan457\">[DATE]</tspan><desc "
        "id=\"desc459\">512: XTEXT_EOC</desc><desc id=\"desc461\">512: XTEXT_EOC</desc><desc "
        "id=\"desc463\">512: XTEXT_EOW</desc><desc id=\"desc465\">512: XTEXT_EOC</desc><desc "
        "id=\"desc467\">512: XTEXT_EOC</desc><desc id=\"desc469\">512: XTEXT_EOC</desc><desc "
        "id=\"desc471\">512: XTEXT_EOC</desc><desc id=\"desc473\">512: XTEXT_EOW</desc><desc "
        "id=\"desc475\">512: XTEXT_EOL</desc><desc id=\"desc477\">512: XTEXT_EOP</desc><desc "
        "id=\"desc479\">512: XTEXT_PAINTSHAPE_END</desc></tspan></tspan></text></g></g><g "
        "class=\"com.sun.star.drawing.TextShape\" id=\"g567\"><g id=\"id10\"><rect "
        "class=\"BoundingBox\" stroke=\"none\" fill=\"none\" x=\"0\" y=\"1\" width=\"9001\" "
        "height=\"726\" id=\"rect490\"/><desc id=\"desc492\">150</desc><desc id=\"desc494\">512: "
        "XTEXT_PAINTSHAPE_BEGIN</desc><text class=\"TextShape\" id=\"text564\"><desc "
        "class=\"Paragraph\" id=\"desc496\"/><tspan class=\"TextParagraph\" "
        "font-family=\"Liberation Sans, sans-serif\" font-size=\"423px\" font-weight=\"700\" "
        "id=\"tspan562\"><desc id=\"desc498\">138</desc><desc id=\"desc500\">136</desc><desc "
        "id=\"desc502\">135</desc><desc id=\"desc504\">134</desc><desc "
        "id=\"desc506\">113</desc><desc class=\"TextPortion\" id=\"desc508\">type: Text; content: "
        "[INVALID_SIGNATURE]; </desc><tspan class=\"TextPosition\" x=\"2180\" y=\"511\" "
        "id=\"tspan560\"><tspan fill=\"rgb(239,65,61)\" stroke=\"none\" "
        "id=\"tspan510\">[INVALID_SIGNATURE]</tspan><desc id=\"desc512\">512: "
        "XTEXT_EOC</desc><desc id=\"desc514\">512: XTEXT_EOC</desc><desc id=\"desc516\">512: "
        "XTEXT_EOW</desc><desc id=\"desc518\">512: XTEXT_EOC</desc><desc id=\"desc520\">512: "
        "XTEXT_EOC</desc><desc id=\"desc522\">512: XTEXT_EOC</desc><desc id=\"desc524\">512: "
        "XTEXT_EOC</desc><desc id=\"desc526\">512: XTEXT_EOC</desc><desc id=\"desc528\">512: "
        "XTEXT_EOC</desc><desc id=\"desc530\">512: XTEXT_EOC</desc><desc id=\"desc532\">512: "
        "XTEXT_EOC</desc><desc id=\"desc534\">512: XTEXT_EOC</desc><desc id=\"desc536\">512: "
        "XTEXT_EOC</desc><desc id=\"desc538\">512: XTEXT_EOC</desc><desc id=\"desc540\">512: "
        "XTEXT_EOC</desc><desc id=\"desc542\">512: XTEXT_EOC</desc><desc id=\"desc544\">512: "
        "XTEXT_EOC</desc><desc id=\"desc546\">512: XTEXT_EOC</desc><desc id=\"desc548\">512: "
        "XTEXT_EOC</desc><desc id=\"desc550\">512: XTEXT_EOC</desc><desc id=\"desc552\">512: "
        "XTEXT_EOW</desc><desc id=\"desc554\">512: XTEXT_EOL</desc><desc id=\"desc556\">512: "
        "XTEXT_EOP</desc><desc id=\"desc558\">512: "
        "XTEXT_PAINTSHAPE_END</desc></tspan></tspan></text></g></g></g></g></g></g></g></svg>");
    return svg;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
