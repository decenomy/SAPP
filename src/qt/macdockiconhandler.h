// Copyright (c) 2011-2020 The Bitcoin developers
// Copyright (c) 2020-2021 The Sapphire Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MACDOCKICONHANDLER_H
#define BITCOIN_QT_MACDOCKICONHANDLER_H

#include <QMainWindow>
#include <QObject>

/** Macintosh-specific dock icon handler.
 */
class MacDockIconHandler : public QObject
{
    Q_OBJECT

public:
    static MacDockIconHandler* instance();
    static void cleanup();

Q_SIGNALS:
    void dockIconClicked();

private:
    MacDockIconHandler();
};

#endif // BITCOIN_QT_MACDOCKICONHANDLER_H
