// Robustness tests of the frame codec: unknown types, empty frames, fragmentation.
// Build: cmake -S tests/protocol -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>
#include "kazaprotocol.h"

class TstKaZaProtocol : public QObject
{
    Q_OBJECT

    QTcpServer m_server;
    QTcpSocket m_peer;          // raw side, writes arbitrary bytes
    QTcpSocket *m_socket {nullptr};
    KaZaProtocol *m_protocol {nullptr};

    static QByteArray frame(quint8 type, const QByteArray &payload)
    {
        QByteArray f;
        QDataStream s(&f, QIODevice::WriteOnly);
        s.setByteOrder(QDataStream::BigEndian);
        s << type << quint32(payload.size());
        f.append(payload);
        return f;
    }

    void send(const QByteArray &bytes)
    {
        m_peer.write(bytes);
        m_peer.flush();
    }

private slots:
    void init()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        m_peer.connectToHost(QHostAddress::LocalHost, m_server.serverPort());
        QVERIFY(m_server.waitForNewConnection(3000));
        m_socket = m_server.nextPendingConnection();
        QVERIFY(m_peer.waitForConnected(3000));
        m_protocol = new KaZaProtocol(m_socket);
    }

    void cleanup()
    {
        delete m_protocol;
        m_protocol = nullptr;
        m_peer.abort();
        delete m_socket;
        m_socket = nullptr;
        m_server.close();
    }

    void commandIsDelivered()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(KaZaProtocol::FRAME_SYSTEM, "PING"));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("PING"));
    }

    // Foundation of the "services" compatibility story: an unknown frame type must be
    // consumed silently, without closing the connection or blocking following frames.
    void unknownFrameTypeIsIgnored()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(100, QByteArray("unknown payload")) + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("AFTER"));
        QCOMPARE(m_socket->state(), QAbstractSocket::ConnectedState);
    }

    void unknownFrameTypeOutsideKnownRangeIsIgnored()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(200, QByteArray(300, 'x')) + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(m_socket->state(), QAbstractSocket::ConnectedState);
    }

    void fragmentedFrameIsReassembled()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameCommand);
        const QByteArray f = frame(KaZaProtocol::FRAME_SYSTEM, "FRAGMENTED");
        for (int i = 0; i < f.size(); ++i) {
            send(f.mid(i, 1));
            QTest::qWait(5);
            if (i < f.size() - 1)
                QCOMPARE(spy.count(), 0);
        }
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("FRAGMENTED"));
    }

    void twoFramesInOneWrite()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(KaZaProtocol::FRAME_SYSTEM, "ONE") + frame(KaZaProtocol::FRAME_SYSTEM, "TWO"));
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("TWO"));
    }

    // ---- Service frames (REQUEST / RESPONSE / CANCEL) ----

    // Writes a response through a second KaZaProtocol and checks the raw bytes decode back.
    KaZaProtocol::ServiceResponse roundTripResponse(const KaZaProtocol::ServiceResponse &in, bool *received)
    {
        QTcpServer srv;
        srv.listen(QHostAddress::LocalHost);
        QTcpSocket tx;
        tx.connectToHost(QHostAddress::LocalHost, srv.serverPort());
        srv.waitForNewConnection(3000);
        QTcpSocket *rxSock = srv.nextPendingConnection();
        tx.waitForConnected(3000);
        KaZaProtocol txProto(&tx);
        KaZaProtocol rxProto(rxSock);
        KaZaProtocol::ServiceResponse out;
        QSignalSpy spy(&rxProto, &KaZaProtocol::frameServiceResponse);
        txProto.sendServiceResponse(in);
        tx.flush();
        *received = spy.wait(3000) || spy.count() > 0;
        if (*received)
            out = spy.at(0).at(0).value<KaZaProtocol::ServiceResponse>();
        delete rxSock;
        return out;
    }

    void requestRoundTrip()
    {
        QTcpSocket tx;
        tx.connectToHost(QHostAddress::LocalHost, m_server.serverPort());
        QVERIFY(m_server.waitForNewConnection(3000));
        QVERIFY(tx.waitForConnected(3000));
        QTcpSocket *s2 = m_server.nextPendingConnection();
        KaZaProtocol rx(s2);
        QSignalSpy spy2(&rx, &KaZaProtocol::frameServiceRequest);
        KaZaProtocol txp(&tx);
        const QDateTime now = QDateTime(QDate(2026, 10, 23), QTime(12, 0), QTimeZone::UTC);
        txp.sendServiceRequest(42, "ecowitt.history",
                               {{"start", now}, {"step", "1 hour"}, {"n", 7}, {"ratio", 0.5}, {"flag", true}});
        tx.flush();
        QTRY_COMPARE(spy2.count(), 1);
        QCOMPARE(spy2.at(0).at(0).toUInt(), 42u);
        QCOMPARE(spy2.at(0).at(1).toString(), QStringLiteral("ecowitt.history"));
        const QVariantMap p = spy2.at(0).at(2).toMap();
        QCOMPARE(p.value("start").toDateTime(), now);
        QCOMPARE(p.value("step").toString(), QStringLiteral("1 hour"));
        QCOMPARE(p.value("n").toInt(), 7);
        QCOMPARE(p.value("ratio").toDouble(), 0.5);
        QCOMPARE(p.value("flag").toBool(), true);
        delete s2;
    }

    void cancelRoundTrip()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameServiceCancel);
        QByteArray payload;
        QDataStream s(&payload, QIODevice::WriteOnly);
        s << quint32(7);
        send(frame(KaZaProtocol::FRAME_CANCEL, payload));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toUInt(), 7u);
    }

    void responseErrorRoundTrip()
    {
        KaZaProtocol::ServiceResponse in;
        in.id = 5;
        in.status = KaZaProtocol::ServiceInvalidParams;
        in.errorCode = "step";
        in.errorMessage = "invalid value";
        bool ok = false;
        const auto out = roundTripResponse(in, &ok);
        QVERIFY(ok);
        QCOMPARE(out.id, 5u);
        QCOMPARE(out.status, quint8(KaZaProtocol::ServiceInvalidParams));
        QCOMPARE(out.errorCode, QStringLiteral("step"));
        QCOMPARE(out.kind, quint8(KaZaProtocol::ServiceKindEmpty));
    }

    void responseSmallTableRoundTrip()
    {
        KaZaProtocol::ServiceResponse in;
        in.id = 6;
        in.kind = KaZaProtocol::ServiceKindTable;
        in.columns = {"t", "v"};
        in.rows = {{QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC), 1.5},
                   {QDateTime(QDate(2026, 1, 2), QTime(0, 0), QTimeZone::UTC), QVariant()}};
        in.truncated = true;
        bool ok = false;
        const auto out = roundTripResponse(in, &ok);
        QVERIFY(ok);
        QCOMPARE(out.kind, quint8(KaZaProtocol::ServiceKindTable));
        QCOMPARE(out.columns, in.columns);
        QCOMPARE(out.rows.size(), 2);
        QCOMPARE(out.rows.at(0).at(1).toDouble(), 1.5);
        QVERIFY(out.rows.at(1).at(1).isNull());
        QVERIFY(out.truncated);
    }

    void responseLargeTableIsCompressed()
    {
        KaZaProtocol::ServiceResponse in;
        in.id = 8;
        in.kind = KaZaProtocol::ServiceKindTable;
        in.columns = {"a", "b"};
        for (int i = 0; i < 5000; ++i)
            in.rows.append({i, double(i) / 3});
        bool ok = false;
        const auto out = roundTripResponse(in, &ok);
        QVERIFY(ok);
        QCOMPARE(out.rows.size(), 5000);
        QCOMPARE(out.rows.at(4999).at(0).toInt(), 4999);
    }

    void responseValueRoundTrip()
    {
        KaZaProtocol::ServiceResponse in;
        in.id = 9;
        in.kind = KaZaProtocol::ServiceKindValue;
        in.value = 12.25;
        bool ok = false;
        const auto out = roundTripResponse(in, &ok);
        QVERIFY(ok);
        QCOMPARE(out.kind, quint8(KaZaProtocol::ServiceKindValue));
        QCOMPARE(out.value.toDouble(), 12.25);
    }

    void oversizedRequestIsIgnored()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameServiceRequest);
        QSignalSpy cmd(m_protocol, &KaZaProtocol::frameCommand);
        QByteArray payload(KaZaProtocol::ServiceMaxRequestSize + 1, 'x');
        send(frame(KaZaProtocol::FRAME_REQUEST, payload) + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTRY_COMPARE(cmd.count(), 1);
        QCOMPARE(spy.count(), 0);
    }

    void malformedRequestIsIgnored()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameServiceRequest);
        QSignalSpy cmd(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(KaZaProtocol::FRAME_REQUEST, QByteArray("\x00\x00\x00\x01\xff", 5))
             + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTRY_COMPARE(cmd.count(), 1);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(m_socket->state(), QAbstractSocket::ConnectedState);
    }

    void tooManyParamsIsIgnored()
    {
        QVariantMap params;
        for (int i = 0; i <= KaZaProtocol::ServiceMaxParams; ++i)
            params.insert(QString("k%1").arg(i), i);
        QByteArray payload;
        QDataStream s(&payload, QIODevice::WriteOnly);
        s.setVersion(QDataStream::Qt_6_0);
        s << quint32(1) << QString("svc") << params;
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameServiceRequest);
        QSignalSpy cmd(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(KaZaProtocol::FRAME_REQUEST, payload) + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTRY_COMPARE(cmd.count(), 1);
        QCOMPARE(spy.count(), 0);
    }

    void decompressionBombIsRejected()
    {
        // Claims 1 GiB uncompressed in the qCompress header
        QByteArray fake;
        QDataStream fs(&fake, QIODevice::WriteOnly);
        fs << quint32(1u << 30);
        fake.append(QByteArray(16, 'x'));
        QByteArray payload;
        QDataStream s(&payload, QIODevice::WriteOnly);
        s.setVersion(QDataStream::Qt_6_0);
        s << quint32(3) << quint8(0) << QString() << QString()
          << quint8(KaZaProtocol::ServiceKindTable | KaZaProtocol::ServiceKindCompressed) << false << fake;
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameServiceResponse);
        QSignalSpy cmd(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(KaZaProtocol::FRAME_RESPONSE, payload) + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTRY_COMPARE(cmd.count(), 1);
        QCOMPARE(spy.count(), 0);
    }

    // Known defect (len < 1 => break in _dataReady): an empty frame stays in the buffer and
    // blocks every following frame. Desired behaviour is that the stream keeps going.
    void emptyFrameDoesNotBlockStream()
    {
        QSignalSpy spy(m_protocol, &KaZaProtocol::frameCommand);
        send(frame(KaZaProtocol::FRAME_SYSTEM, QByteArray()) + frame(KaZaProtocol::FRAME_SYSTEM, "AFTER"));
        QTest::qWait(300);
        QEXPECT_FAIL("", "known defect: empty frame blocks the stream", Continue);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TstKaZaProtocol)
#include "tst_kazaprotocol.moc"
