#ifndef LANGUAGEENVIRONMENT_H
#define LANGUAGEENVIRONMENT_H

#include <QProcessEnvironment>
#include <QStringList>

namespace LanguageEnvironment
{
QStringList preferredLanguages();
QStringList translationLanguages(const QStringList &codes);
void apply(QProcessEnvironment &environment, const QStringList &codes);
void apply(QProcessEnvironment &environment);
}

#endif
