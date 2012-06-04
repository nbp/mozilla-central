
// This script is used from within browser_gcli_edit.html

window.addEventListener('load', function() {
  var pid = document.getElementById('pid');
  var h3 = document.createElement('h3');
  h3.id = 'h3id';
  h3.classList.add('h3class');
  h3.appendChild(document.createTextNode('h3'));
  h3.setAttribute('data-a1', 'h3');
  pid.parentNode.appendChild(h3);
});
