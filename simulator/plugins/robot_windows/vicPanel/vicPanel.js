/* global webots */
var held = null;
var repeatTimer = null;

function send(msg) {
  window.robotWindow.send(msg);
}

function release() {
  if (held === null)
    return;
  send(held.split(':')[0] + ':0');
  held = null;
  if (repeatTimer !== null) {
    clearInterval(repeatTimer);
    repeatTimer = null;
  }
}

function press(msg) {
  release();
  held = msg;
  send(msg);
  repeatTimer = setInterval(function() { send(msg); }, 200);
}

function fakeonboard() {
  fetch("http://localhost:8888/consolefunccall", {
    method: 'POST',
    body: "func=Exit Onboarding - Mark Complete&args="
  });
}

window.onload = function() {
  window.robotWindow = webots.window('vicPanel');
  window.robotWindow.setTitle('Vector Panel');

  var buttons = document.getElementsByTagName('button');
  for (var i = 0; i < buttons.length; ++i) {
    var b = buttons[i];
    var oneshot = b.getAttribute('data-oneshot');
    if (oneshot !== null) {
      b.onclick = (function(msg) { return function() { send(msg); }; })(oneshot);
      continue;
    }
    b.onmousedown = (function(msg) { return function() { press(msg); }; })(b.getAttribute('data-msg'));
    b.onmouseup = release;
    b.onmouseleave = release;
  }
  document.onmouseup = release;

  var boxes = [['petBox', 'pet'], ['chgBox', 'chg']];
  for (var j = 0; j < boxes.length; ++j) {
    (function(box, key) {
      box.onchange = function() {
        send(box.checked ? key + ':1' : key + ':0');
      };
      setInterval(function() {
        if (box.checked)
          send(key + ':1');
      }, 200);
    })(document.getElementById(boxes[j][0]), boxes[j][1]);
  }
};
