#include <QSignalSpy>
#include <QTest>

#include "outputmodel.h"

// Displays side by side from x = 0, each its own logical display, display 0 primary.
static DisplayClient::State extendedDisplays(int count)
{
    DisplayClient::State state;
    for (int i = 0; i < count; ++i) {
        cutefish::display::ipc::PhysicalDisplay physical;
        physical.sessionId = QString::number(i);
        cutefish::display::ipc::Mode mode;
        mode.width = 1920;
        mode.height = 1080;
        mode.refreshMilliHz = 60000;
        mode.current = true;
        physical.modes.append(mode);
        state.physical.append(physical);
        cutefish::display::ipc::LogicalDisplay logical;
        logical.id = physical.sessionId;
        logical.destinations = {physical.sessionId};
        logical.primary = i == 0;
        logical.scale120 = 120;
        logical.x = i * 1920;
        state.logical.append(logical);
    }
    return state;
}

class OutputModelTest : public QObject
{
    Q_OBJECT
private slots:

    void stateRefreshKeepsRowsAndLayout()
    {
        DisplayClient::State state;
        for (int i = 0; i < 3; ++i) {
            cutefish::display::ipc::PhysicalDisplay physical;
            physical.sessionId = QString::number(i);
            cutefish::display::ipc::Mode mode;
            mode.width = 1920;
            mode.height = 1080;
            mode.refreshMilliHz = 60000;
            mode.current = true;
            physical.modes.append(mode);
            state.physical.append(physical);
            cutefish::display::ipc::LogicalDisplay logical;
            logical.id = physical.sessionId;
            logical.destinations = {physical.sessionId};
            logical.primary = i == 0;
            logical.scale120 = 120;
            logical.x = i == 1 ? -1920 : 0;
            logical.y = i == 2 ? 1080 : 0;
            state.logical.append(logical);
        }
        state.placements = {{"1", "0", 3, 0}, {"2", "0", 2, 0}};
        DisplayOutputModel model;
        model.setState(state);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        const QPersistentModelIndex selected(model.index(1));
        QVERIFY(model.setData(selected, 1.25, DisplayOutputModel::ScaleRole));
        const auto placements = model.placements();
        QCOMPARE(placements.size(), 2);
        QCOMPARE(placements[1].parentId, QString("0"));
        QCOMPARE(placements[1].edge, 2u);
        std::reverse(state.physical.begin(), state.physical.end());
        model.setState(state);
        QCOMPARE(reset.count(), 0);
        QVERIFY(selected.isValid());
        QCOMPARE(model.data(selected, DisplayOutputModel::IdRole).toString(), QString("1"));
    }

    void onlyModeChangesNeedConfirmation()
    {
        DisplayClient::State state = extendedDisplays(2);
        state.physical[0].connectionType = 1;
        for (auto &physical : state.physical) {
            cutefish::display::ipc::Mode mode;
            mode.width = 1280;
            mode.height = 720;
            mode.refreshMilliHz = 60000;
            physical.modes.append(mode);
        }
        DisplayOutputModel model;
        model.setState(state);
        QVERIFY(!model.needsConfirmation());

        QVERIFY(model.setData(model.index(1), 1.25, DisplayOutputModel::ScaleRole));
        QVERIFY(model.dirty());
        QVERIFY(!model.needsConfirmation());

        // The internal panel is no exception.
        QVERIFY(model.setData(model.index(0), 1, DisplayOutputModel::ResolutionIndexRole));
        QVERIFY(model.needsConfirmation());
        QVERIFY(model.setData(model.index(0), 0, DisplayOutputModel::ResolutionIndexRole));
        QVERIFY(!model.needsConfirmation());

        QVERIFY(model.setData(model.index(1), 1, DisplayOutputModel::ResolutionIndexRole));
        QVERIFY(model.needsConfirmation());

        QVERIFY(model.setData(model.index(1), false, DisplayOutputModel::EnabledRole));
        QVERIFY(!model.needsConfirmation());

        model.discard();
        QVERIFY(!model.needsConfirmation());

        // Switching a display on picks a mode without one to fall back to.
        for (auto &mode : state.physical[1].modes)
            mode.current = false;
        state.logical.removeLast();
        model.setState(state);
        QVERIFY(model.setData(model.index(1), true, DisplayOutputModel::EnabledRole));
        QVERIFY(model.dirty());
        QVERIFY(!model.needsConfirmation());
    }

    void mirroringThePrimaryHandsItToTheSource()
    {
        DisplayOutputModel model;
        model.setState(extendedDisplays(3));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        // DisplayScreen applies on dirtyChanged, so the handoff has to come before it.
        QString applied;
        connect(&model, &DisplayOutputModel::dirtyChanged, [&] {
            applied = model.primary();
        });
        QVERIFY(model.setData(model.index(0), 1, DisplayOutputModel::ReplicationSourceIndexRole));
        QCOMPARE(applied, QString("1"));
        QCOMPARE(model.primary(), QString("1"));
        QVERIFY(!model.data(model.index(0), DisplayOutputModel::PrimaryRole).toBool());
        QVERIFY(model.data(model.index(1), DisplayOutputModel::PrimaryRole).toBool());
        bool primaryChanged = false;
        for (const auto &arguments : changed) {
            const auto roles = arguments.at(2).value<QList<int>>();
            primaryChanged |= roles.contains(DisplayOutputModel::PrimaryRole);
        }
        QVERIFY(primaryChanged);
        const auto placements = model.placements();
        QCOMPARE(placements.size(), 1);
        QCOMPARE(placements[0].displayId, QString("2"));
        QCOMPARE(placements[0].parentId, QString("1"));
    }

    void mirroringThePrimaryNeedsAnExtendedSource()
    {
        // Display 1 is off in the first state and mirrors display 2 in the second.
        auto off = extendedDisplays(3);
        off.logical.removeAt(1);
        auto mirroring = off;
        mirroring.logical[1].destinations.append("1");
        for (const auto &state : {off, mirroring}) {
            DisplayOutputModel model;
            model.setState(state);
            QVERIFY(model.setData(model.index(0), 1,
                                  DisplayOutputModel::ReplicationSourceIndexRole));
            QCOMPARE(model.primary(), QString("0"));
            QVERIFY(model.data(model.index(0), DisplayOutputModel::PrimaryRole).toBool());
        }
    }

    void selectedResolutionAndRefreshStayConsistent()
    {
        DisplayClient::State state;
        cutefish::display::ipc::PhysicalDisplay physical;
        physical.sessionId = "display-1";
        cutefish::display::ipc::Mode mode;
        mode.width = 1920;
        mode.height = 1080;
        mode.refreshMilliHz = 60000;
        mode.current = true;
        physical.modes.append(mode);
        mode.current = false;
        mode.refreshMilliHz = 144000;
        physical.modes.append(mode);
        mode.width = 1280;
        mode.height = 720;
        mode.refreshMilliHz = 60000;
        physical.modes.append(mode);
        state.physical.append(physical);
        DisplayOutputModel model;
        model.setState(state);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        const auto row = model.index(0);
        QVERIFY(model.setData(row, 1, DisplayOutputModel::RefreshRateIndexRole));
        QCOMPARE(model.requests().first().mode.refreshMilliHz, 144000);
        QVERIFY(model.setData(row, 1, DisplayOutputModel::ResolutionIndexRole));
        QCOMPARE(model.requests().first().mode.width, 1280);
        QCOMPARE(model.data(row, DisplayOutputModel::RefreshRateIndexRole).toInt(), 0);
        QCOMPARE(model.data(row, DisplayOutputModel::RefreshRatesRole).toStringList().size(), 1);
        const auto roles = changed.last().at(2).value<QList<int>>();
        QVERIFY(roles.contains(DisplayOutputModel::RefreshRatesRole));
        QVERIFY(roles.contains(DisplayOutputModel::RefreshRateIndexRole));
    }

    void externalStateDoesNotBecomeAnEdit()
    {
        DisplayOutputModel model;
        bool edit = false;
        connect(&model, &DisplayOutputModel::dirtyChanged, [&] {
            edit |= model.dirty();
        });
        model.setState({});
        QVERIFY(!edit);
    }

    void recommendationsFollowTheCompositor()
    {
        DisplayClient::State state;
        cutefish::display::ipc::PhysicalDisplay physical;
        physical.sessionId = QStringLiteral("0");
        physical.recommendedScale120 = 180;
        for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160), QSize(2560, 1440)}) {
            cutefish::display::ipc::Mode mode;
            mode.width = size.width();
            mode.height = size.height();
            mode.refreshMilliHz = 60000;
            mode.preferred = size.width() == 3840;
            mode.current = size.width() == 1920;
            physical.modes.append(mode);
        }
        state.physical.append(physical);
        cutefish::display::ipc::LogicalDisplay logical;
        logical.id = physical.sessionId;
        logical.destinations = {physical.sessionId};
        state.logical.append(logical);

        DisplayOutputModel model;
        model.setState(state);
        const auto row = model.index(0);
        // Largest first: 3840 × 2160 heads the list.
        QCOMPARE(model.data(row, DisplayOutputModel::RecommendedResolutionIndexRole).toInt(), 0);
        QCOMPARE(model.data(row, DisplayOutputModel::RecommendedScaleRole).toDouble(), 1.5);
    }

    void screensAreNamedForPeople()
    {
        DisplayClient::State state;
        const auto add = [&](const QString &connector, const QString &monitor, uint type) {
            cutefish::display::ipc::PhysicalDisplay physical;
            physical.sessionId = connector;
            physical.connectorName = connector;
            physical.model = monitor;
            physical.connectionType = type;
            state.physical.append(physical);
            cutefish::display::ipc::LogicalDisplay logical;
            logical.id = connector;
            logical.destinations = {connector};
            state.logical.append(logical);
        };
        add("eDP-1", "eDP-1", 1);
        add("DP-1", "DELL U2720Q", 2);
        add("DP-2", "DELL U2720Q", 2);
        add("HDMI-A-1", "HDMI-A-1", 2);

        DisplayOutputModel model;
        model.setState(state);
        QStringList names;
        for (int row = 0; row < model.rowCount(); ++row)
            names.append(model.data(model.index(row), Qt::DisplayRole).toString());
        QCOMPARE(names, QStringList({"Built-in Display", "DELL U2720Q (DP-1)",
                                     "DELL U2720Q (DP-2)", "HDMI-A-1"}));
        QCOMPARE(model.data(model.index(3), DisplayOutputModel::ReplicationSourceModelRole)
                         .toStringList(),
                 QStringList({"Extend", "Built-in Display", "DELL U2720Q (DP-1)",
                              "DELL U2720Q (DP-2)"}));
    }
};

QTEST_GUILESS_MAIN(OutputModelTest)
#include "outputmodel_test.moc"
