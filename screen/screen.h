#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "displayclient.h"

class DisplayOutputModel;

class KScreenScreen : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Screen)
    Q_PROPERTY(DisplayOutputModel *outputModel READ outputModel CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool pendingConfirmation READ pendingConfirmation NOTIFY stateChanged)
    Q_PROPERTY(int confirmationSeconds READ confirmationSeconds NOTIFY stateChanged)

public:
    explicit KScreenScreen(QObject *parent = nullptr);

    DisplayOutputModel *outputModel() const { return m_outputModel; }

    bool available() const { return m_client.available(); }

    bool busy() const { return m_busy; }

    bool dirty() const;
    bool canApply() const;

    QString error() const { return m_error; }

    bool pendingConfirmation() const { return !m_client.state().previewToken.isEmpty(); }

    int confirmationSeconds() const;

    Q_INVOKABLE void apply();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void keep();
    Q_INVOKABLE void revert();

    Q_INVOKABLE void save() { apply(); }

    Q_INVOKABLE void identify();

signals:
    void stateChanged();
    void identifyRequested(QStringList displayIds);

private:
    void updateState();

    DisplayClient m_client;
    DisplayOutputModel *m_outputModel = nullptr;
    QTimer m_countdown;
    QString m_instanceId;
    qulonglong m_topologyGeneration = 0;
    QString m_error;
    bool m_busy = false;
};
