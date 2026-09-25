#include <QSignalSpy>
#include <QTest>

#include "outputmodel.h"

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
