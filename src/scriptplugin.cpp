/***************************************************************************
                   scriptplugin.cpp  -  description
                             -------------------
    begin                : Fri Nov 9 2007
    copyright            : (C) 2007 by Dominik Seichter
    email                : domseichter@web.de
    copyright            : (C) 2020 by Harald Sitter <sitter@kde.org>
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "scriptplugin.h"

#include <kconfiggroup.h>
#include <kiconloader.h>
#include <kmessagebox.h>
#include <KIO/StoredTransferJob>
#include <KIO/StatJob>
#include <KJobWidgets>
#include <kio_version.h>

#include <QTemporaryFile>
#include <QFile>
#include <QMenu>
#include <QTextStream>
#include <QVariant>
#include <QFileDialog>

#include <kjs/kjsinterpreter.h>

#include "ui_scriptplugindialog.h"
#include "ui_scriptpluginwidget.h"
#include "batchrenamer.h"
#include "krenamefile.h"

const char *ScriptPlugin::s_pszFileDialogLocation = "kfiledialog://krenamejscript";
const char *ScriptPlugin::s_pszVarNameIndex       = "krename_index";
const char *ScriptPlugin::s_pszVarNameUrl         = "krename_url";
const char *ScriptPlugin::s_pszVarNameFilename    = "krename_filename";
const char *ScriptPlugin::s_pszVarNameExtension   = "krename_extension";
const char *ScriptPlugin::s_pszVarNameDirectory   = "krename_directory";

enum EVarType {
    eVarType_String = 0,
    eVarType_Int,
    eVarType_Double,
    eVarType_Bool
};

ScriptPlugin::ScriptPlugin(PluginLoader *loader)
    : QObject(),
      Plugin(loader), m_parent(nullptr)
{
    m_name = i18n("JavaScript Plugin");
    m_icon = "applications-development";
    m_interpreter = new KJSInterpreter();
    m_menu   = new QMenu();
    m_widget = new Ui::ScriptPluginWidget();

    this->addSupportedToken("js;.*");

    m_help.append("[js;4+5];;" + i18n("Insert a snippet of JavaScript code (4+5 in this case)"));

    m_menu->addAction(i18n("Index of the current file"),     this, SLOT(slotInsertIndex()));
    m_menu->addAction(i18n("URL of the current file"),       this, SLOT(slotInsertUrl()));
    m_menu->addAction(i18n("Filename of the current file"),  this, SLOT(slotInsertFilename()));
    m_menu->addAction(i18n("Extension of the current file"), this, SLOT(slotInsertExtension()));
    m_menu->addAction(i18n("Directory of the current file"), this, SLOT(slotInsertDirectory()));
}

ScriptPlugin::~ScriptPlugin()
{
    delete m_widget;
    delete m_menu;
    delete m_interpreter;
}

QString ScriptPlugin::processFile(BatchRenamer *b, int index,
                                  const QString &filenameOrToken, EPluginType)
{
    QString token(filenameOrToken);
    QString script;
    QString definitions = m_widget->textCode->toPlainText();

    if (token.contains(";")) {
        script = token.section(';', 1);   // all sections from 1 to the last
        token  = token.section(';', 0, 0).toLower();
    } else {
        token = token.toLower();
    }

    if (token == "js") {
        // Setup interpreter
        const KRenameFile &file = b->files()->at(index);
        initKRenameVars(file, index);

        // Make sure definitions are executed first
        script = definitions + '\n' + script;

        KJSResult result = m_interpreter->evaluate(script, nullptr);
        if (result.isException()) {
            qDebug("JavaScript Error: %s", result.errorMessage().toUtf8().data());
            return QString();
        }

        return result.value().toString(m_interpreter->globalContext());
    }

    return QString();
}

const QPixmap ScriptPlugin::icon() const
{
    return QIcon::fromTheme(m_icon).pixmap(KIconLoader::SizeSmall);
}

void ScriptPlugin::createUI(QWidget *parent) const
{
    QStringList labels;
    labels << i18n("Variable Name");
    labels << i18n("Initial Value");

    const_cast<ScriptPlugin *>(this)->m_parent = parent;
    m_widget->setupUi(parent);
    m_widget->listVariables->setColumnCount(2);
    m_widget->listVariables->setHeaderLabels(labels);

    connect(m_widget->listVariables, &QTreeWidget::itemSelectionChanged,
            this, &ScriptPlugin::slotEnableControls);
    connect(m_widget->buttonAdd, &QPushButton::clicked,
            this, &ScriptPlugin::slotAdd);
    connect(m_widget->buttonRemove, &QPushButton::clicked,
            this, &ScriptPlugin::slotRemove);
    connect(m_widget->buttonLoad, &QPushButton::clicked,
            this, &ScriptPlugin::slotLoad);
    connect(m_widget->buttonSave, &QPushButton::clicked,
            this, &ScriptPlugin::slotSave);
    connect(m_widget->textCode, &QTextEdit::textChanged,
            this, &ScriptPlugin::slotEnableControls);

    const_cast<ScriptPlugin *>(this)->slotEnableControls();

    m_widget->buttonLoad->setIcon(QIcon::fromTheme("document-open"));
    m_widget->buttonSave->setIcon(QIcon::fromTheme("document-save-as"));
    m_widget->buttonAdd->setIcon(QIcon::fromTheme("list-add"));
    m_widget->buttonRemove->setIcon(QIcon::fromTheme("list-remove"));

    m_widget->buttonInsert->setMenu(m_menu);
}

void ScriptPlugin::initKRenameVars(const KRenameFile &file, int index)
{
    // KRename definitions
    m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
            ScriptPlugin::s_pszVarNameIndex,
            index);
    m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
            ScriptPlugin::s_pszVarNameUrl,
            file.srcUrl().url());
    m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
            ScriptPlugin::s_pszVarNameFilename,
            file.srcFilename());
    m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
            ScriptPlugin::s_pszVarNameExtension,
            file.srcExtension());
    m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
            ScriptPlugin::s_pszVarNameDirectory,
            file.srcDirectory());

    // User definitions, set them only on first file
    if (index != 0) {
        return;
    }

    for (int i = 0; i < m_widget->listVariables->topLevelItemCount(); i++) {
        // TODO, we have to know the type of the variable!
        QTreeWidgetItem *item = m_widget->listVariables->topLevelItem(i);
        if (!item) {
            continue;
        }

        EVarType eVarType = static_cast<EVarType>(item->data(1, Qt::UserRole).toInt());
        const QString &name  = item->text(0);
        const QString &value = item->text(1);
        switch (eVarType) {
        default:
        case eVarType_String:
            m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
                    name, value.toUtf8().data());
            break;
        case eVarType_Int:
            m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
                    name, value.toInt());
            break;
        case eVarType_Double:
            m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
                    name, value.toDouble());
            break;
        case eVarType_Bool:
            m_interpreter->globalObject().setProperty(m_interpreter->globalContext(),
                    name,
                    (value.toLower() == "true" ? true : false));
            break;
        }
    }
}

void ScriptPlugin::insertVariable(const char *name)
{
    m_widget->textCode->insertPlainText(QString(name));
}

void ScriptPlugin::slotEnableControls()
{
    bool bEnable = !(m_widget->listVariables->selectedItems().isEmpty());
    m_widget->buttonRemove->setEnabled(bEnable);

    bEnable = !m_widget->textCode->toPlainText().isEmpty();
    m_widget->buttonSave->setEnabled(bEnable);
}

void ScriptPlugin::slotAdd()
{
    QDialog dialog;
    Ui::ScriptPluginDialog dlg;

    dlg.setupUi(&dialog);
    dlg.comboType->addItem(i18n("String"), eVarType_String);
    dlg.comboType->addItem(i18n("Int"), eVarType_Int);
    dlg.comboType->addItem(i18n("Double"), eVarType_Double);
    dlg.comboType->addItem(i18n("Boolean"), eVarType_Bool);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString name  = dlg.lineName->text();
    QString value = dlg.lineValue->text();

    // Build a Java script statement
    QString script = name + " = " + value + ';';

    KJSInterpreter interpreter;
    KJSResult result = m_interpreter->evaluate(script, nullptr);
    if (result.isException()) {
        KMessageBox::error(m_parent,
                            i18n("A JavaScript error has occurred: ") +
                            result.errorMessage(), this->name());
    } else {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, name);
        item->setText(1, value);
        item->setData(1, Qt::UserRole, dlg.comboType->currentData());

        m_widget->listVariables->addTopLevelItem(item);
    }
}

void ScriptPlugin::slotRemove()
{
    QTreeWidgetItem *item = m_widget->listVariables->currentItem();
    if (item) {
        m_widget->listVariables->invisibleRootItem()->removeChild(item);
        delete item;
    }
}

void ScriptPlugin::slotLoad()
{
    if (!m_widget->textCode->toPlainText().isEmpty() &&
            KMessageBox::questionYesNo(m_parent,
                                       i18n("All currently entered definitions will be lost. Do you want to continue?"))
            == KMessageBox::No) {
        return;
    }

    QUrl url = QFileDialog::getOpenFileUrl(m_parent, i18n("Select file"),
                                           QUrl(ScriptPlugin::s_pszFileDialogLocation));

    if (!url.isEmpty()) {
        // Also support remote files
        KIO::StoredTransferJob *job = KIO::storedGet(url);
        KJobWidgets::setWindow(job, m_parent);
        if (job->exec()) {
            m_widget->textCode->setPlainText(QString::fromLocal8Bit(job->data()));
        } else {
            KMessageBox::error(m_parent, job->errorString());
        }
    }

    slotEnableControls();
}

void ScriptPlugin::slotSave()
{
    QUrl url = QFileDialog::getSaveFileUrl(m_parent, i18n("Select file"),
                                           QUrl(ScriptPlugin::s_pszFileDialogLocation));

    if (!url.isEmpty()) {
#if KIO_VERSION >= QT_VERSION_CHECK(5, 69, 0)
        KIO::StatJob *statJob = KIO::statDetails(url, KIO::StatJob::DestinationSide, KIO::StatNoDetails);
#else
        KIO::StatJob *statJob = KIO::stat(url, KIO::StatJob::DestinationSide, 0);
#endif
        statJob->exec();
        if (statJob->error() != KIO::ERR_DOES_NOT_EXIST) {
            int m = KMessageBox::warningYesNo(m_parent, i18n("The file %1 already exists. "
                                              "Do you want to overwrite it?", url.toDisplayString(QUrl::PreferLocalFile)));

            if (m == KMessageBox::No) {
                return;
            }
        }

        if (url.isLocalFile()) {
            QFile file(url.path());
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << m_widget->textCode->toPlainText();
                out.flush();
                file.close();
            } else {
                KMessageBox::error(m_parent, i18n("Unable to open %1 for writing.", url.path()));
            }
        } else {
            KIO::StoredTransferJob *job = KIO::storedPut(m_widget->textCode->toPlainText().toLocal8Bit(), url, -1);
            KJobWidgets::setWindow(job, m_parent);
            job->exec();
            if (job->error()) {
                KMessageBox::error(m_parent, job->errorString());
            }
        }
    }

    slotEnableControls();
}

void ScriptPlugin::slotTest()
{
}

void ScriptPlugin::slotInsertIndex()
{
    this->insertVariable(ScriptPlugin::s_pszVarNameIndex);
}

void ScriptPlugin::slotInsertUrl()
{
    this->insertVariable(ScriptPlugin::s_pszVarNameUrl);
}

void ScriptPlugin::slotInsertFilename()
{
    this->insertVariable(ScriptPlugin::s_pszVarNameFilename);
}

void ScriptPlugin::slotInsertExtension()
{
    this->insertVariable(ScriptPlugin::s_pszVarNameExtension);
}

void ScriptPlugin::slotInsertDirectory()
{
    this->insertVariable(ScriptPlugin::s_pszVarNameDirectory);
}

void ScriptPlugin::loadConfig(KConfigGroup &group)
{
    QStringList  variableNames;
    QStringList  variableValues;
    QVariantList variableTypes;

    variableNames  = group.readEntry("JavaScriptVariableNames",  variableNames);
    variableValues = group.readEntry("JavaScriptVariableValues", variableValues);
    variableTypes  = group.readEntry("JavaScriptVariableTypes", variableTypes);

    int min = qMin(variableNames.count(), variableValues.count());
    min = qMin(min, variableTypes.count());

    for (int i = 0; i < min; i++) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, variableNames[i]);
        item->setText(1, variableValues[i]);
        item->setData(1, Qt::UserRole, variableTypes[i]);

        m_widget->listVariables->addTopLevelItem(item);
    }

    m_widget->textCode->setPlainText(group.readEntry("JavaScriptDefinitions", QString()));
}

void ScriptPlugin::saveConfig(KConfigGroup &group) const
{
    QStringList  variableNames;
    QStringList  variableValues;
    QVariantList variableTypes;

    for (int i = 0; i < m_widget->listVariables->topLevelItemCount(); i++) {
        QTreeWidgetItem *item = m_widget->listVariables->topLevelItem(i);
        if (item) {
            variableNames  << item->text(0);
            variableValues << item->text(1);
            variableTypes  << item->data(1, Qt::UserRole);
        }
    }

    group.writeEntry("JavaScriptVariableNames",  variableNames);
    group.writeEntry("JavaScriptVariableValues", variableValues);
    group.writeEntry("JavaScriptVariableTypes",  variableTypes);
    group.writeEntry("JavaScriptDefinitions", m_widget->textCode->toPlainText());
}

