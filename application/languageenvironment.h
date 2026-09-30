#ifndef LANGUAGEENVIRONMENT_H
#define LANGUAGEENVIRONMENT_H

#include <QProcessEnvironment>
#include <QStringList>

namespace LanguageEnvironment
{
// The language generated in /etc/locale.gen, until the user picks one.
QString systemLanguage();
QStringList preferredLanguages();
QStringList translationLanguages(const QStringList &codes);
void apply(QProcessEnvironment &environment, const QStringList &codes);
void apply(QProcessEnvironment &environment);
}

#endif
