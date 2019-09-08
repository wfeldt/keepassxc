/*
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Import.h"

#include <QCommandLineParser>
#include <QHash>

#include "cli/TextStream.h"
#include "cli/Utils.h"
#include "core/Database.h"
#include "core/Merger.h"

#include "format/KdbxXmlReader.h"

#include <cstdlib>

Import::Import()
{
    name = QString("import");
    description = QObject::tr("Import XML data into database.");
}

Import::~Import()
{
}

int Import::execute(const QStringList& arguments)
{
    TextStream outputTextStream(Utils::STDOUT, QIODevice::WriteOnly);
    TextStream errorTextStream(Utils::STDERR, QIODevice::WriteOnly);

    QCommandLineParser parser;
    parser.setApplicationDescription(description);
    parser.addPositionalArgument("database", QObject::tr("Path of the database to import into."));
    parser.addPositionalArgument("xml_file", QObject::tr("Path of the XML file with additional data."));
    parser.addOption(Command::QuietOption);

    QCommandLineOption mergeModeOption(QStringList() << "merge-mode",
                                          QObject::tr("Merge mode to apply."),
                                          QObject::tr("mode"));
    parser.addOption(mergeModeOption);
    parser.addOption(Command::KeyFileOption);
    parser.addOption(Command::NoPasswordOption);

    parser.addHelpOption();
    parser.process(arguments);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 2) {
        errorTextStream << parser.helpText().replace("[options]", "import [options]");
        return EXIT_FAILURE;
    }

    Group::MergeMode merge_mode(Group::Synchronize);

    QHash<QString, Group::MergeMode> merge_modes({
      {"", Group::Default},
      {"synchronize", Group::Synchronize},
      {"duplicate", Group::Duplicate},
      {"keeplocal", Group::KeepLocal},
      {"keepremote", Group::KeepRemote},
    });

    QString merge_mode_str(parser.value(mergeModeOption));

    if (!merge_modes.contains(merge_mode_str)) {
        errorTextStream << "Valid merge modes are:" << endl
                        << "  synchronize (default), duplicate, keeplocal, keepremote" << endl;
        return EXIT_FAILURE;
    }

    merge_mode = merge_modes[merge_mode_str];

    auto db1 = Utils::unlockDatabase(args.at(0),
                                     !parser.isSet(Command::NoPasswordOption),
                                     parser.value(Command::KeyFileOption),
                                     parser.isSet(Command::QuietOption) ? Utils::DEVNULL : Utils::STDOUT,
                                     Utils::STDERR);
    if (!db1) {
        return EXIT_FAILURE;
    }

    KdbxXmlReader reader(KeePass2::FILE_VERSION_4);
    auto db2 = reader.readDatabase(args.at(1));
    if(reader.hasError()) {
        errorTextStream << QObject::tr("Error reading XML file:\n%1").arg(reader.errorString());
        return EXIT_FAILURE;
    }

    Merger merger(db2.data(), db1.data());
    merger.setForcedMergeMode(merge_mode);
    bool databaseChanged = merger.merge();

    if (databaseChanged) {
        QString errorMessage;
        if (!db1->save(args.at(0), &errorMessage, true, false)) {
            errorTextStream << QObject::tr("Unable to save database to file : %1").arg(errorMessage) << endl;
            return EXIT_FAILURE;
        }
        if (!parser.isSet(Command::QuietOption)) {
            outputTextStream << "Successfully imported XML data." << endl;
        }
    } else if (!parser.isSet(Command::QuietOption)) {
        outputTextStream << "Database was not modified." << endl;
    }

    return EXIT_SUCCESS;
}
