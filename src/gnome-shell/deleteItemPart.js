/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const St = imports.gi.St;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const DeleteButton = Me.imports.deleteButton;

var GPasteDeleteItemPart = class {
    constructor(client, index) {
        this.actor = new St.Bin({ x_align: St.Align.END });
        this._deleteButton = new DeleteButton.GPasteDeleteButton(client, index);
        this.actor.child = this._deleteButton.actor;
    }

    setIndex(index) {
        this._deleteButton.setIndex(index);
    }
};
