#include "tray/diff_html.hpp"

#include <QByteArray>

#include <string>

namespace revisa::tray {

namespace {

const QString kClose = QStringLiteral("</span>");
// Between the removed and the inserted text of a replacement, so «e̶he» reads «e̶ he»: a thin space,
// only on screen (the pasted text comes from applyEnabled).
const QString kGap = QStringLiteral("&#8201;");

QString escaped(const QString& text) {
    QString out = text.toHtmlEscaped();
    out.replace(QLatin1String("\r\n"), QLatin1String("<br>"));
    out.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    return out;
}

bool isEnabled(const std::vector<bool>& enabled, std::size_t i) { return i >= enabled.size() || enabled[i]; }

// A side of an edit as shown to the user: spaces visible when the side is only whitespace.
QString visible(const QString& side) {
    QString out = side;
    if (side.trimmed().isEmpty()) out.replace(QLatin1Char(' '), QChar(0x2423));  // ␣
    return out;
}

QString quoted(const QString& side) {
    if (side.isEmpty()) return QStringLiteral("(nada)");
    return QStringLiteral("«%1»").arg(visible(side));
}

}  // namespace

std::size_t codePoints(const QString& text) {
    std::size_t n = 0;
    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size; ++i) {
        if (text[i].isHighSurrogate() && i + 1 < size && text[i + 1].isLowSurrogate()) ++i;
        ++n;
    }
    return n;
}

QString diffHtml(const QString& original, const std::vector<check::Edit>& edits,
                 const std::vector<bool>& enabled, const DiffColors& colors) {
    const QString delOpen =
        QStringLiteral("<span style=\"color:%1;background-color:%2;text-decoration:line-through\">")
            .arg(colors.delFg, colors.delBg);
    const QString insOpen = QStringLiteral("<span style=\"color:%1;background-color:%2;font-weight:600\">")
                                .arg(colors.insFg, colors.insBg);
    const auto size = static_cast<std::size_t>(original.size());
    QString out = QStringLiteral("<div style=\"line-height:145%\"><span style=\"white-space:pre-wrap\">");
    std::size_t pos = 0;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const check::Edit& e = edits[i];
        if (!isEnabled(enabled, i) || e.start < pos || e.end < e.start || e.end > size) continue;
        out += escaped(original.mid(static_cast<qsizetype>(pos), static_cast<qsizetype>(e.start - pos)));
        const QString from = original.mid(static_cast<qsizetype>(e.start), static_cast<qsizetype>(e.end - e.start));
        const QString to = QString::fromStdString(e.to);
        if (!from.isEmpty()) out += delOpen + escaped(from) + kClose;
        if (!from.isEmpty() && !to.isEmpty()) out += kGap;
        if (!to.isEmpty()) out += insOpen + escaped(to) + kClose;
        pos = e.end;
    }
    out += escaped(original.mid(static_cast<qsizetype>(pos)));
    out += kClose + QStringLiteral("</div>");
    return out;
}

QString editLabel(const check::Edit& edit) {
    return quoted(QString::fromStdString(edit.from)) + QStringLiteral(" → ") +
           quoted(QString::fromStdString(edit.to));
}

QString editTitleHtml(const check::Edit& edit, const QString& muted, const QString& strong) {
    const QString from = QString::fromStdString(edit.from);
    const QString to = QString::fromStdString(edit.to);
    const QString oldSide = QStringLiteral("<span style=\"color:%1;text-decoration:line-through\">%2</span>")
                                .arg(muted, visible(from).toHtmlEscaped());
    const QString newSide =
        QStringLiteral("<span style=\"color:%1;font-weight:600\">%2</span>").arg(strong, visible(to).toHtmlEscaped());
    const QString word = QStringLiteral("<span style=\"color:%1\">%2</span>");
    if (from.isEmpty()) return word.arg(muted, QStringLiteral("Añadir&nbsp;")) + newSide;
    if (to.isEmpty()) return word.arg(muted, QStringLiteral("Quitar&nbsp;")) + oldSide;
    return oldSide + word.arg(muted, QStringLiteral("&nbsp;&nbsp;→&nbsp;&nbsp;")) + newSide;
}

QString typeLabel(catalog::Type type) {
    using catalog::Type;
    switch (type) {
        case Type::Espaciado: return QStringLiteral("Espaciado");
        case Type::Mayuscula: return QStringLiteral("Mayúscula");
        case Type::TildeDiacritica: return QStringLiteral("Tilde diacrítica");
        case Type::TildeInterrogativa: return QStringLiteral("Tilde interrogativa");
        case Type::PorQue: return QStringLiteral("Porque / por qué");
        case Type::TildeFalta: return QStringLiteral("Falta tilde");
        case Type::TildeSobra: return QStringLiteral("Sobra tilde");
        case Type::HaberAVer: return QStringLiteral("Haber / a ver");
        case Type::VerboHaberH: return QStringLiteral("Verbo haber");
        case Type::EchoHecho: return QStringLiteral("Echo / hecho");
        case Type::HayAhiAy: return QStringLiteral("Hay / ahí / ay");
        case Type::SinoSiNo: return QStringLiteral("Sino / si no");
        case Type::JuntoSeparado: return QStringLiteral("Junto o separado");
        case Type::BV: return QStringLiteral("B o V");
        case Type::GJ: return QStringLiteral("G o J");
        case Type::H: return QStringLiteral("Con o sin h");
        case Type::LlY: return QStringLiteral("LL o Y");
        case Type::CZSX: return QStringLiteral("C, Z, S o X");
        case Type::SignoApertura: return QStringLiteral("Signo de apertura");
        case Type::Errata: return QStringLiteral("Errata");
        case Type::Dequeismo: return QStringLiteral("Dequeísmo");
        case Type::Queismo: return QStringLiteral("Queísmo");
        case Type::Leismo: return QStringLiteral("Leísmo");
        case Type::Laismo: return QStringLiteral("Laísmo");
        case Type::Loismo: return QStringLiteral("Loísmo");
        case Type::HaberImpersonal: return QStringLiteral("Haber impersonal");
        case Type::Imperativo: return QStringLiteral("Imperativo");
        case Type::PreteritoS: return QStringLiteral("Pretérito sin -s");
        case Type::Conjugacion: return QStringLiteral("Conjugación");
        case Type::Concordancia: return QStringLiteral("Concordancia");
        case Type::ComaSobra: return QStringLiteral("Coma de más");
        case Type::Otro: return QStringLiteral("Otra corrección");
    }
    return QStringLiteral("Corrección");
}

QString correctionsCount(std::size_t n) {
    return n == 1 ? QStringLiteral("1 corrección") : QStringLiteral("%1 correcciones").arg(n);
}

QString applyEnabled(const QString& original, const std::vector<check::Edit>& edits,
                     const std::vector<bool>& enabled) {
    std::vector<check::Edit> kept;
    for (std::size_t i = 0; i < edits.size(); ++i)
        if (isEnabled(enabled, i)) kept.push_back(edits[i]);
    const QByteArray utf8 = original.toUtf8();
    const std::string result = check::apply(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), kept);
    return QString::fromStdString(result);
}

}  // namespace revisa::tray
