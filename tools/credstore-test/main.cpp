/*
  Credential Store Test Utility

  Tests the AgentCredentialStore keychain integration.
  Requires QApplication on macOS (qtkeychain uses GCD which needs NSApplication).

  Usage:
    credstore-test read       — Read the stored refresh token
    credstore-test write TOK  — Write a refresh token
    credstore-test delete     — Delete the stored refresh token
    credstore-test info       — Show backend info (keychain vs QSettings)

  Build:
    cd ai-qlcplus && ./build.sh
    # Binary: build/tools/credstore-test/credstore-test
    # Run:    DYLD_LIBRARY_PATH=build/engine/src ./build/tools/credstore-test/credstore-test info

  Manual keychain management (bypasses this tool):

    macOS:
      # List:   security find-generic-password -s "bunnyhole-ai" -a "refresh_token"
      # Delete: security delete-generic-password -s "bunnyhole-ai" -a "refresh_token"
      # Or use Keychain Access.app → search "bunnyhole-ai"

    Linux:
      # List:   secret-tool search service bunnyhole-ai
      # Delete: secret-tool clear service bunnyhole-ai

    Windows:
      # List:   cmdkey /list | findstr bunnyhole
      # Delete: cmdkey /delete:bunnyhole-ai
*/

#include <QTimer>
#include <QDebug>

/*
 * macOS requirement: qtkeychain uses Grand Central Dispatch internally,
 * which requires NSApplication (via QGuiApplication or QApplication).
 * QCoreApplication alone causes the finished signal to never fire.
 * See: https://claudiocambra.com/2023/03/21/fixing-qtkeychain-freezing-on-apple-devices/
 */
#if defined(__APPLE__)
#include <QApplication>
#else
#include <QCoreApplication>
#endif

#include "agentcredentialstore.h"

int main(int argc, char *argv[])
{
#if defined(__APPLE__)
    QApplication app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif
    app.setApplicationName("credstore-test");
    app.setOrganizationName("qlcplus");

    if (argc < 2) {
        qDebug() << "Usage: credstore-test <read|write|delete|info> [token]";
        return 1;
    }

    QString command = argv[1];
    AgentCredentialStore store;

    qDebug() << "Backend:" << (store.isSecureBackend() ? "OS Keychain" : "QSettings (insecure)");

    if (command == "info") {
        return 0;
    }

    if (command == "read") {
        QObject::connect(&store, &AgentCredentialStore::refreshTokenLoaded,
                         [&](const QString &token) {
            if (token.isEmpty())
                qDebug() << "No refresh token stored.";
            else
                qDebug() << "Refresh token:" << token.left(20) << "..." << "(" << token.length() << "chars)";
            app.quit();
        });
        // Schedule after event loop starts
        QTimer::singleShot(0, &store, &AgentCredentialStore::loadRefreshToken);
    }
    else if (command == "write") {
        if (argc < 3) {
            qDebug() << "Usage: credstore-test write <token>";
            return 1;
        }
        QString token = argv[2];
        QObject::connect(&store, &AgentCredentialStore::refreshTokenSaved,
                         [&](bool success, const QString &error) {
            if (success)
                qDebug() << "Refresh token saved.";
            else
                qDebug() << "Failed to save:" << error;
            app.quit();
        });
        QTimer::singleShot(0, [&store, token]() { store.saveRefreshToken(token); });
    }
    else if (command == "delete") {
        QObject::connect(&store, &AgentCredentialStore::refreshTokenDeleted,
                         [&](bool success, const QString &error) {
            if (success)
                qDebug() << "Refresh token deleted.";
            else
                qDebug() << "Failed to delete:" << error;
            app.quit();
        });
        QTimer::singleShot(0, &store, &AgentCredentialStore::deleteRefreshToken);
    }
    else {
        qDebug() << "Unknown command:" << command;
        qDebug() << "Usage: credstore-test <read|write|delete|info> [token]";
        return 1;
    }

    // Timeout safety — don't hang forever
    QTimer::singleShot(5000, [&]() {
        qDebug() << "Timed out waiting for keychain response.";
        app.exit(1);
    });

    return app.exec();
}
