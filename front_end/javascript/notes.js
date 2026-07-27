// =============================================
// notes.js  —  Per-bookmark notes (localStorage)
// =============================================

'use strict';

import {getActiveDbIndex} from './data.js';

function storageKey(dbIdx)
{
	return `localmarks-notes-${dbIdx}`;
}

function readAll(dbIdx)
{
	try {
		return JSON.parse(localStorage.getItem(storageKey(dbIdx)) || '{}');
	} catch {
		return {};
	}
}

function writeAll(dbIdx, notes)
{
	localStorage.setItem(storageKey(dbIdx), JSON.stringify(notes));
}

function noteKey(url)
{
	return url;
}

export function getNote(url, dbIdx)
{
	if (dbIdx === undefined) dbIdx = getActiveDbIndex();
	const notes = readAll(dbIdx);
	return notes[noteKey(url)] || null;
}

export function setNote(url, text, dbIdx)
{
	if (dbIdx === undefined) dbIdx = getActiveDbIndex();
	const notes = readAll(dbIdx);
	const key   = noteKey(url);

	if (!text || !text.trim()) {
		delete notes[key];
	} else {
		notes[key] = {text: text.trim(), updated: Date.now()};
	}
	writeAll(dbIdx, notes);
}

export function deleteNote(url, dbIdx)
{
	setNote(url, '', dbIdx);
}

export function hasNote(url, dbIdx)
{
	return !!getNote(url, dbIdx);
}
