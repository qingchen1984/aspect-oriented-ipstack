/*
 * Copyright 2013 David Graeff
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE REGENTS AND CONTRIBUTORS, INC. OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "pickdependencies.h"
#include "options.h"
#include "filemodel/fileModel.h"
#include "filemodel/filemodelitem.h"
#include "familymodel/familyModel.h"
#include "familymodel/familyFile.h"
#include "familymodel/familyComponent.h"
#include "kconfigmodel/kconfigModel.h"
#include "problem_list_item.h"
#include "filterproxymodel.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QDebug>
#include <QSettings>
#include <QCloseEvent>

MainWindow::MainWindow(Options* o, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow), options(o)
{
    ui->setupUi(this);
    setWindowTitle(tr("%1 - %2[*]").arg(qApp->applicationName()).
                   arg(QFileInfo(QString::fromStdString(options->featureToFilesRelationfile)).fileName()));
    on_btnShowProblems_toggled(false);
    on_btnShowLog_toggled(false);
    on_btnShowUnusedFiles_toggled(false);

    { // restore window geometry settings
        QSettings settings;
        restoreGeometry(settings.value("geometry").toByteArray());
        restoreState(settings.value("windowState").toByteArray());

        ui->btnShowComponentDetails->setChecked(settings.value("btnShowComponentDetails").toBool());
        ui->btnShowLog->setChecked(settings.value("btnShowLog").toBool());
        ui->btnShowMissingDepends->setChecked(settings.value("btnShowMissingDepends").toBool());
        ui->btnShowProblems->setChecked(settings.value("btnShowProblems").toBool());
        ui->btnShowUnusedFiles->setChecked(settings.value("btnShowUnusedFiles").toBool());
    }

    QAction* sep = new QAction(this);
    sep->setSeparator(true);
    // prepare component tree
    ui->treeComponents->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->treeComponents->addActions(QList<QAction*>() << ui->actionAdd_component
                                   << ui->actionRemove_selected_components
                                   << sep
                                   << ui->actionExpand_all << ui->actionCollapse_all
                                   << ui->actionExpand_all_missing_files_only
                                   << sep);

    // prepare problems list
    ui->listProblems->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->listProblems->addActions(QList<QAction*>() << ui->actionBe_smart << ui->actionBe_smart_Selection_only);

    // dependency model
    dependsModel = new DependencyModel(QString::fromStdString(options->kconfigfile),this);
    dependsModelProxy = new FilterProxyModel(this);
    dependsModelProxy->setSourceModel(dependsModel);
    dependsModelProxy->setDynamicSortFilter(true);
    dependsModelProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(ui->lineSearchMissingDepends,&QLineEdit::textChanged,
            dependsModelProxy, &FilterProxyModel::setFilterFixedString);
    ui->treeDependencies->setModel(dependsModelProxy);
    ui->treeDependencies->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // file models
    filemodel = new FileModel(QString::fromStdString(options->base_directory), this);
    filemodelProxy = new FilterProxyModel(this);
    filemodelProxy->setSourceModel(filemodel);
    filemodelProxy->setDynamicSortFilter(true);
    filemodelProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(ui->lineSearchUnusedFiles,&QLineEdit::textChanged,
            filemodelProxy, &FilterProxyModel::setFilterFixedString);
    ui->treeFiles->setModel(filemodelProxy);
    connect(filemodel,&FileModel::unused_files_update,ui->treeFiles,&QTreeView::expandAll);

    // component model
    componentModel = new FamilyModel(QString::fromStdString(options->base_directory),
                                        QString::fromStdString(options->featureToFilesRelationfile), this);
    componentModelProxy = new FilterProxyModel(this);
    componentModelProxy->setSourceModel(componentModel);
    componentModelProxy->setDynamicSortFilter(true);
    componentModelProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(ui->lineSearchComponents,&QLineEdit::textChanged,
            componentModelProxy, &FilterProxyModel::setFilterFixedString);

    ui->treeComponents->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->treeComponents->setModel(componentModelProxy);
    ui->treeComponents->expand(componentModelProxy->index(0,0));
    connect(ui->treeComponents->selectionModel(),&QItemSelectionModel::currentChanged,
            this, &MainWindow::familyModelSelectionChanged);

    connect(componentModel,&FamilyModel::removed_existing_files,filemodel,&FileModel::addFiles);
    connect(componentModel,&FamilyModel::added_files,filemodel,&FileModel::removeFiles);
    connect(componentModel,&FamilyModel::rejected_existing_files,this,&MainWindow::rejected_files);
	connect(componentModel,&FamilyModel::changed,this,&MainWindow::familyModelChanged);
    connect(componentModel,&FamilyModel::updateDependency,dependsModel,&DependencyModel::updateDependency);

    // problems
    connect(ui->listProblems->selectionModel(),&QItemSelectionModel::currentChanged,
            this, &MainWindow::problemSelectionChanged);

    // init
    on_actionSynchronize_with_file_system_triggered();
    familyModelSelectionChanged(QModelIndex());
    update_problems();
	ui->actionSave->setEnabled(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    { // save window geometry settings
        QSettings settings;
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
        settings.setValue("btnShowComponentDetails", ui->btnShowComponentDetails->isChecked());
        settings.setValue("btnShowLog", ui->btnShowLog->isChecked());
        settings.setValue("btnShowMissingDepends", ui->btnShowMissingDepends->isChecked());
        settings.setValue("btnShowProblems", ui->btnShowProblems->isChecked());
        settings.setValue("btnShowUnusedFiles", ui->btnShowUnusedFiles->isChecked());
    }

    if (!ui->actionSave->isEnabled()) {
        e->accept();
        return;
    }
    QMessageBox mb(tr("Save configuration?"), tr("Save configuration?"), QMessageBox::Warning,
            QMessageBox::Yes | QMessageBox::Default, QMessageBox::No, QMessageBox::Cancel | QMessageBox::Escape);
    mb.setButtonText(QMessageBox::Yes, tr("&Save changes"));
    mb.setButtonText(QMessageBox::No, tr("&Discard changes"));
    mb.setButtonText(QMessageBox::Cancel, tr("Cancel Exit"));
    switch (mb.exec()) {
    case QMessageBox::Yes:
        on_actionSave_triggered();
        e->accept();
        break;
    case QMessageBox::No:
        e->accept();
        break;
    case QMessageBox::Cancel:
        e->ignore();
        break;
    }
}

void MainWindow::solveProblems(const QList<ProblemListItem*>& problems)
{
    if (componentModel->rowCount()>0 &&
            QMessageBox::warning(this, tr("Be smart!"),
                  tr("This will remove all missing files, and correct paths for moved files. It also removes empty components and unneccessary nested subcomponents."),
                    QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel) == QMessageBox::Yes) {


        for(int i=0;i<problems.count();++i) {
            ProblemListItem* item = problems[i];

            switch (item->type) {
                case ProblemListItem::COMPONENT_WITH_ONLY_FILES: {
                    FamilyComponent* i = (FamilyComponent*)item->problem_component_item.data();
                    // Add all files of the item to the parent item
                    i->parent->addFiles(i->get_all_files(true), FamilyComponent::AllowMultipleSubdirLevels);
                    // Remove from parent and delete item
                    i->parent->childs.removeOne(i);
                    delete i;
                break; }
                case ProblemListItem::EMPTY_COMPONENT:
                    componentModel->removeComponent((FamilyComponent*)item->problem_component_item.data());
                break;
                case ProblemListItem::MOVED_FILE: {
                    FamilyFile* i = (FamilyFile*)item->problem_component_item.data();
                    FamilyComponent* parent = i->parent;
                    componentModel->removeFile(i);
                    parent->addFiles(QStringList() << item->maybe_moved, FamilyComponent::AllowMultipleSubdirLevels);

                break;}
                case ProblemListItem::REMOVED_FILE:
                    componentModel->removeFile((FamilyFile*)item->problem_component_item.data());
                break;
                case ProblemListItem::UNUSED_FILES:
                case ProblemListItem::MISSING_DEPENDENCIES:
                break;
            }
        }

        // update the view by emitting reset signals
        componentModel->clear(false);

        // update problems section by rescanning files
        on_actionSynchronize_with_file_system_triggered();
    }
}

void MainWindow::familyModelChanged() {
    setWindowModified(true);
	ui->actionSave->setEnabled(true);
}

void MainWindow::update_problems()
{
    ui->listProblems->clear();

    if (filemodel->get_unused_files_size()) {
        auto* problem_item = new ProblemListItem(ProblemListItem::UNUSED_FILES);
        problem_item->setIcon(QIcon::fromTheme("dialog-information"));
        problem_item->setText(tr("%1 files in your project directory are not used by any component!").
                              arg(filemodel->get_unused_files_size()));
        ui->listProblems->addItem(problem_item);
    }

    if (dependsModel->countUnused()) {
        auto* problem_item = new ProblemListItem(ProblemListItem::MISSING_DEPENDENCIES);
        problem_item->setIcon(QIcon::fromTheme("dialog-information"));
        problem_item->setText(tr("%1 kconfig dependencies are not used!").
                              arg(dependsModel->countUnused()));
        ui->listProblems->addItem(problem_item);
    }

    QModelIndexList list_of_missing_files = componentModel->get_missing_files();
    foreach (const QModelIndex& index, list_of_missing_files) {
        auto* fileitem = static_cast<FamilyFile*>(index.internalPointer());

        QList<FileModelItem*> paths = filemodel->get_file_path(fileitem->filename);
        // could be moved instead of removed
        if (paths.size()) {
            auto* problem_item = new ProblemListItem(ProblemListItem::MOVED_FILE);
            problem_item->maybe_moved = paths[0]->full_absolute_path();
            problem_item->setIcon(QIcon::fromTheme("dialog-question"));
            problem_item->setText(tr("File probably moved from %1 to %2").
                              arg(componentModel->relative_directory(fileitem->get_full_path())).
                              arg(componentModel->relative_directory(problem_item->maybe_moved)));
            problem_item->problem_component_item = fileitem;
            ui->listProblems->addItem(problem_item);
        } else {
            auto* problem_item = new ProblemListItem(ProblemListItem::REMOVED_FILE);
            problem_item->setIcon(QIcon::fromTheme("dialog-warning"));
            problem_item->setText(tr("File missing: %1").
                              arg(componentModel->relative_directory(fileitem->get_full_path())));
            problem_item->problem_component_item = fileitem;
            ui->listProblems->addItem(problem_item);
        }
    }

    // Detect components that only contains files and empty components
    QList<FamilyBaseItem*> queue;
    queue << componentModel->getRootItem();
    while (queue.size()) {
        FamilyBaseItem* source = queue.takeFirst();
        if (source->parent && source->type == FamilyComponent::TYPE) {
            if (source->childs.isEmpty()) {
                auto* problem_item = new ProblemListItem(ProblemListItem::EMPTY_COMPONENT);
                problem_item->setIcon(QIcon::fromTheme("dialog-warning"));
                problem_item->setText(tr("Component empty: Parent %1").arg(source->parent->get_component_name()));
                problem_item->problem_component_item = source;
                ui->listProblems->addItem(problem_item);
            } else if (source->parent && source->childs[0]->type==FamilyFile::TYPE &&
                       static_cast<FamilyComponent*>(source)->get_vname().isEmpty() &&
                       static_cast<FamilyComponent*>(source)->get_dependencies().isEmpty() &&
                       static_cast<FamilyComponent*>(source)->get_directory() == source->parent->get_directory()) {
                auto* problem_item = new ProblemListItem(ProblemListItem::COMPONENT_WITH_ONLY_FILES);
                problem_item->setIcon(QIcon::fromTheme("dialog-warning"));
                problem_item->setText(tr("Files-Only component unneccesary: Parent %1").arg(source->parent->get_component_name()));
                problem_item->problem_component_item = source;
                ui->listProblems->addItem(problem_item);
            }
        }

        foreach(FamilyBaseItem* sourceChild, source->childs) {
            // add to queue
            queue << sourceChild;
        }
    }

    // update button icon
    ui->btnShowProblems->setText(tr("%1 Problems").arg(ui->listProblems->count()));
    bool have_problems = ui->listProblems->count();
    ui->btnShowProblems->setEnabled(have_problems);
    ui->menuProblems->setEnabled(have_problems);
    ui->btnShowProblems->setEnabled(have_problems);
    if (ui->btnShowProblems->isChecked() && !have_problems)
        ui->btnShowProblems->setChecked(false);
}

void MainWindow::add_log(const QString &msg)
{
    if (msg.size())
        ui->listLog->insertItem(0,msg);

    ui->btnShowLog->setText(tr("%1 Log messages").arg(ui->listLog->count()));
}

void MainWindow::on_actionQuit_triggered()
{
    close();
}

void MainWindow::on_actionSave_triggered()
{
    setWindowModified(false);
    ui->actionSave->setEnabled(false);
    componentModel->save();
}

void MainWindow::on_actionSynchronize_with_file_system_triggered()
{
    filemodel->createFileTree();
    dependsModel->createModel();
    // Get all used kconfig dependencies and mark them as used in the dependency model
    QStringList deps = componentModel->get_dependencies();
    foreach (const QString& dep, deps) {
        dependsModel->updateDependency(QString(), dep);
    }

    add_log(tr("Scanned %1 files in project directory").arg(filemodel->get_unused_files_size()));
    // Get all used files and remove them from the file model
    filemodel->removeFiles(componentModel->get_used_files());
    ui->treeFiles->expandAll();
    update_problems();
}

void MainWindow::on_actionAdd_component_triggered()
{
    FamilyComponent* currentComponent;
    if (!ui->treeComponents->currentIndex().isValid())
        currentComponent=componentModel->getRootItem();
    else
        currentComponent=(FamilyComponent*)componentModelProxy->mapToSource(ui->treeComponents->currentIndex()).internalPointer();

    focusComponent(componentModel->addComponent(currentComponent));
    add_log(tr("Added component to %1").arg(currentComponent->parent->get_component_name()));
}

void MainWindow::on_actionRemove_selected_components_triggered()
{
    QModelIndexList list = ui->treeComponents->selectionModel()->selectedRows();
    for (int i = list.size() - 1; i >= 0; --i) {
        QModelIndex index = list[i];
        componentModelProxy->removeRow(index.row(), index.parent());
    }

    // update problems and log
    update_problems();
}

void MainWindow::familyModelSelectionChanged(const QModelIndex &index)
{
    if (!index.isValid()) {
        ui->btnChangeSubdir->setEnabled(false);
        ui->btnChangeDepends->setEnabled(false);
        ui->actionRemove_selected_components->setEnabled(false);
        return;
    }

    ui->actionRemove_selected_components->setEnabled(true);

    FamilyBaseItem* b = static_cast<FamilyBaseItem*>(componentModelProxy->mapToSource(index).internalPointer());
    if (b->type == FamilyComponent::TYPE) {
        FamilyComponent* currentComponent = (FamilyComponent*)b;

        ui->labelDepends->setText(currentComponent->get_dependencies());
        ui->labelPath->setText(componentModel->relative_directory(currentComponent->get_directory().absolutePath()));
        ui->lineName->blockSignals(true); // do not update the component by the value we set now
        ui->lineName->setText(currentComponent->get_vname());
        ui->lineName->blockSignals(false);
        ui->lineName->setEnabled(true);
        ui->btnChangeSubdir->setEnabled(true);
        ui->btnChangeDepends->setEnabled(true);
    } else {
        FamilyFile* currentComponentFile = (FamilyFile*)b;
        FamilyComponent* currentComponent = currentComponentFile->parent;

        ui->labelDepends->blockSignals(true);
        ui->labelDepends->setText(currentComponent->get_dependencies());
        ui->labelDepends->blockSignals(false);
        ui->labelPath->setText(componentModel->relative_directory(currentComponentFile->get_full_path()));
        ui->lineName->setEnabled(false);
        ui->lineName->blockSignals(true); // do not update the component by the value we set now
        ui->lineName->setText(QString());
        ui->lineName->blockSignals(false);
        ui->btnChangeSubdir->setEnabled(false);
        ui->btnChangeDepends->setEnabled(false);
    }
}

void MainWindow::problemSelectionChanged(const QModelIndex &)
{
    int number_of_items = ui->listProblems->selectedItems().size();
    ui->actionBe_smart_Selection_only->setEnabled(number_of_items);
    ui->actionBe_smart_Selection_only->setText(tr("Be smart! (%1 items)").arg(number_of_items));
}

void MainWindow::on_btnChangeSubdir_clicked()
{
    if (!ui->treeComponents->currentIndex().isValid())
        return;
    FamilyComponent* currentComponent=(FamilyComponent*)componentModelProxy->mapToSource(ui->treeComponents->currentIndex()).internalPointer();
    Q_ASSERT(currentComponent);

    QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"),
                                                currentComponent->get_directory().absolutePath(),
                                                QFileDialog::ShowDirsOnly
                                                | QFileDialog::DontResolveSymlinks);

    if (dir.isEmpty())
        return;

    /**
     * If we change the subdirectory, all files of this component (and child components)
     * should to be removed from the component model. We ask the user.
     */
    bool removeFiles = false;
    if (currentComponent->childs.size()) {
        int r = QMessageBox::question(this,tr("Change subdir?"),
                                            tr("You are about to change a subdirectory of a component, where files are listed.\n"
                                             "All filepaths would be invalid after the change!\n"
                                             "Remove file entries?"),
                                            tr("Remove entries"),
                                            tr("Leave entries"),
                                            tr("Cancel"));
        if (r == 0) {
            removeFiles = true;
            add_log(tr("Remove files of %s").arg(currentComponent->get_component_name()));
        } if (r == 2) {
            add_log(tr("Aborted changing a directory"));
            return;
        }
    }
    currentComponent->set_directory(dir,removeFiles);
    ui->labelPath->setText(currentComponent->get_component_dir());
}

void MainWindow::on_btnChangeDepends_clicked()
{
    if (!ui->treeComponents->currentIndex().isValid())
        return;
    FamilyComponent* currentComponent=(FamilyComponent*)componentModelProxy->mapToSource(ui->treeComponents->currentIndex()).internalPointer();
    Q_ASSERT(currentComponent);

    PickDependencies* p = new PickDependencies(dependsModel);
    p->set_initial_selection(currentComponent->get_dependencies());
    if (p->exec() == QDialog::Accepted) {
        currentComponent->set_dependencies(p->get_dependency_string());
        ui->labelDepends->blockSignals(true);
        ui->labelDepends->setText(currentComponent->get_dependencies());
        ui->labelDepends->blockSignals(false);
    }
    p->deleteLater();
}

void MainWindow::on_lineName_textChanged(const QString &arg1)
{
    if (!ui->treeComponents->currentIndex().isValid())
        return;
    FamilyComponent* currentComponent=(FamilyComponent*)componentModelProxy->mapToSource(ui->treeComponents->currentIndex()).internalPointer();
    Q_ASSERT(currentComponent);

    currentComponent->set_vname(arg1);
}

void MainWindow::on_labelDepends_textChanged(const QString &arg1)
{
    if (!ui->treeComponents->currentIndex().isValid())
        return;
    FamilyComponent* currentComponent=(FamilyComponent*)componentModelProxy->mapToSource(ui->treeComponents->currentIndex()).internalPointer();
    Q_ASSERT(currentComponent);

    currentComponent->set_dependencies(arg1);
}

void MainWindow::on_actionExpand_all_missing_files_only_triggered()
{
    ui->treeComponents->setUpdatesEnabled(false);
    QModelIndexList list_of_missing_files = componentModel->get_missing_files();
    foreach (const QModelIndex& index, list_of_missing_files) {
         ui->treeComponents->scrollTo(componentModelProxy->mapFromSource(index));
    }
    ui->treeComponents->setUpdatesEnabled(true);
}

void MainWindow::on_actionClear_and_propose_component_structure_triggered()
{
    if (componentModel->rowCount()==0 ||
            QMessageBox::warning(this, tr("Erase all components?"),
                  tr("This will erase all components and propose a structure based on the kconfig fm file. Start from scratch?"),
                    QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel) == QMessageBox::Yes) {

        ui->treeComponents->setUpdatesEnabled(false);
        add_log(tr("Clear structure and propose one based on the kconfig feature model"));
        componentModel->clear();

        DependencyModel* dmodel = new DependencyModel(QString::fromStdString(options->kconfigfile));
        QList<QPair<DependencyModelItem*, FamilyComponent* > > queue;
        queue << QPair<DependencyModelItem*, FamilyComponent* >(dmodel->getRootItem(), componentModel->getRootItem());
        while (queue.size()) {
            QPair<DependencyModelItem*, FamilyComponent* > p = queue.takeFirst();
            DependencyModelItem* source = p.first;
            FamilyComponent* target = p.second;

            foreach(DependencyModelItem* sourceChild, source->childs) {
                FamilyComponent* targetChild = FamilyComponent::createComponent(componentModel, QString::fromStdString(options->base_directory),target);
                // name
                targetChild->set_vname(sourceChild->name);
                if (targetChild->get_vname().isEmpty())
                    targetChild->set_vname(sourceChild->depends);
                // depends
                targetChild->set_dependencies("&" + sourceChild->depends);

                // add to queue
                queue << QPair<DependencyModelItem*, FamilyComponent* >
                        (sourceChild, targetChild);
            }
        }

        delete dmodel;
        // update the view by emitting reset signals
        componentModel->clear(false);
        // allow updates on the components tree view again
        ui->treeComponents->setUpdatesEnabled(true);
        // update problems section by rescanning files
        on_actionSynchronize_with_file_system_triggered();
    }
}

void MainWindow::on_actionClear_use_filesystem_based_structure_triggered()
{
    if (componentModel->rowCount()==0 ||
            QMessageBox::warning(this, tr("Erase all components?"),
                  tr("This will erase all components and propose a structure based on the filesystem. Start from scratch?"),
                    QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel) == QMessageBox::Yes) {

        ui->treeComponents->setUpdatesEnabled(false);
        add_log(tr("Clear structure and propose one based on the filesystem"));
        componentModel->clear();
        filemodel->createFileTree();

        QList<QPair<FileModelItem*, FamilyComponent* > > queue;
        queue << QPair<FileModelItem*, FamilyComponent* >(filemodel->getRootItem(), componentModel->getRootItem());
        while (queue.size()) {
            QPair<FileModelItem*, FamilyComponent* > p = queue.takeFirst();
            FileModelItem* source = p.first;
            FamilyComponent* target = p.second;

            foreach(FileModelItem* sourceChild, source->childs) {
                if (sourceChild->isFile) {
                    FamilyFile::createFile(componentModel, sourceChild->name,target);
                    continue;
                }

                FamilyComponent* targetChild = FamilyComponent::createComponent(componentModel, sourceChild->full_absolute_path(),target);
                // name
                targetChild->set_vname(sourceChild->name);

                // add to queue
                queue << QPair<FileModelItem*, FamilyComponent* >
                        (sourceChild, targetChild);
            }
        }

        // update the view by emitting reset signals
        componentModel->clear(false);
        // remove used files from the unused-files model
        filemodel->removeFiles(componentModel->get_used_files());
        // allow updates on the components tree view again
        ui->treeComponents->setUpdatesEnabled(true);
        // update problems section by rescanning files
        on_actionSynchronize_with_file_system_triggered();
        // if no unused files, we hide the unused-files panel
        if (filemodel->get_unused_files_size()==0)
            ui->btnShowUnusedFiles->setChecked(false);
    }
}

void MainWindow::on_btnShowUnusedFiles_toggled(bool checked)
{
    ui->btnShowUnusedFiles->setChecked(checked);
    if (checked) {
        ui->btnShowMissingDepends->setChecked(false);
        ui->groupMissingDependencies->setVisible(false);
    }
    ui->groupUnusedFiles->setVisible(checked);
    ui->widgetRight->setVisible(ui->btnShowUnusedFiles->isChecked()||ui->btnShowMissingDepends->isChecked());
}

void MainWindow::on_btnShowMissingDepends_toggled(bool checked)
{
    ui->btnShowMissingDepends->setChecked(checked);
    if (checked) {
        ui->btnShowUnusedFiles->setChecked(false);
        ui->groupUnusedFiles->setVisible(false);
    }
    ui->groupMissingDependencies->setVisible(checked);
    ui->widgetRight->setVisible(ui->btnShowUnusedFiles->isChecked()||ui->btnShowMissingDepends->isChecked());

}

void MainWindow::on_btnShowProblems_toggled(bool checked)
{
    ui->btnShowProblems->setChecked(checked);
    if (checked) {
        ui->btnShowLog->setChecked(false);
        ui->listLog->setVisible(!checked);
        ui->listProblems->setVisible(checked);
    }
    ui->widgetBottom->setVisible(ui->btnShowProblems->isChecked()||ui->btnShowLog->isChecked());
}

void MainWindow::on_btnShowLog_toggled(bool checked)
{
    ui->btnShowLog->setChecked(checked);
    if (checked) {
        ui->btnShowProblems->setChecked(false);
        ui->listLog->setVisible(checked);
        ui->listProblems->setVisible(!checked);
    }
    ui->widgetBottom->setVisible(ui->btnShowProblems->isChecked()||ui->btnShowLog->isChecked());
}

void MainWindow::on_listProblems_activated(const QModelIndex &index)
{
    ProblemListItem* item = (ProblemListItem*)ui->listProblems->item(index.row());
    if(item->type==ProblemListItem::UNUSED_FILES) {
        ui->btnShowUnusedFiles->setChecked(true);
    } else if(item->type==ProblemListItem::MISSING_DEPENDENCIES) {
        ui->btnShowMissingDepends->setChecked(true);
    } else if (item->problem_component_item.isNull()) {
        ui->listProblems->removeItemWidget(item);
    } else {
        focusComponent(item->problem_component_item.data());
    }
}

void MainWindow::focusComponent(FamilyBaseItem* item)
{
    Q_ASSERT(item);
    QModelIndex compModelIndex = componentModel->indexOf(item);
    compModelIndex = componentModelProxy->mapFromSource(compModelIndex);
    ui->treeComponents->scrollTo(compModelIndex);
    ui->treeComponents->selectionModel()->select(compModelIndex,QItemSelectionModel::ClearAndSelect);
    //familyModelSelectionChanged(compModelIndex);
}

void MainWindow::on_actionBe_smart_triggered()
{
    QList<ProblemListItem*> problems;
    for(int i=1;i<ui->listProblems->count();++i) {
        ProblemListItem* item = (ProblemListItem*)ui->listProblems->item(i);
        if (item->problem_component_item.isNull()) {
            ui->listProblems->removeItemWidget(item);
            continue;
        }
        problems.append(item);
    }
    solveProblems(problems);
}


void MainWindow::on_actionBe_smart_Selection_only_triggered()
{
    QList<ProblemListItem*> problems;
    for(int i=1;i<ui->listProblems->selectedItems().count();++i) {
        QListWidgetItem* listitem = ui->listProblems->selectedItems()[i];
        ProblemListItem* item = (ProblemListItem*)listitem;
        if (item->problem_component_item.isNull()) {
            ui->listProblems->removeItemWidget(item);
            continue;
        }
        problems.append(item);
    }
    solveProblems(problems);
}

void MainWindow::rejected_files(const QDir& subdir, const QStringList &files)
{
    // Add files back to filemodel
    filemodel->addFiles(files);

    foreach(const QString& file, files) {
        add_log(tr("File is not below the \"%1\" subdirectory: %2").
                arg(componentModel->relative_directory(subdir.absolutePath())).
                arg(componentModel->relative_directory(file)));
    }
}

