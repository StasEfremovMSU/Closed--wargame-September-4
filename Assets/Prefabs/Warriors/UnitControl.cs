using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class UnitControl : MonoBehaviour {

    public Camera camera;
	// Use this for initialization
	void Start () {
	}
	
	// Update is called once per frame
	void Update () {
        if (gameObject.GetComponent<UnitSoldiers>().control == true)
        {

            /// Когда юнит под управлением человека
            GameStatus.listItem = gameObject.GetComponent<UnitSoldiers>().listItem;

            bool run = false;

            if (Input.GetKey(KeyCode.UpArrow))
            {
                transform.Translate(Vector3.forward * 2 * gameObject.GetComponent<UnitSoldiers>().speed * Time.deltaTime);
                run = true;
            }

            if (Input.GetKey(KeyCode.DownArrow))
            {
                transform.Translate(-Vector3.forward * 2 * gameObject.GetComponent<UnitSoldiers>().speed * Time.deltaTime);
                run = true;
            }

            if (run == true)
            {
                gameObject.GetComponent<UnitSoldiers>().my_animator.SetBool("Run", true);
            }
            else
            {
                gameObject.GetComponent<UnitSoldiers>().my_animator.SetBool("Run", false);
            }

            if (Input.GetKey(KeyCode.LeftArrow))
                transform.Rotate(Vector3.up, -28f * Time.deltaTime);

            if (Input.GetKey(KeyCode.RightArrow))
                transform.Rotate(Vector3.up, +28f * Time.deltaTime);

            // Сдвигаем камеру к юниту
            Vector3 tt = transform.position; tt.y += 2;

            //Debug.Log(transform.position);
            Vector3 np = transform.position - transform.forward * 2 + new Vector3(0, 3, 0);
            //Debug.Log(np);

            Camera.main.transform.position = np;
            Camera.main.transform.rotation = transform.rotation;


            if (GameStatus.ControlReset == true)
            {
                Camera.main.transform.position = GameStatus.Cam1;
                Camera.main.transform.rotation = GameStatus.Cam2;
                gameObject.GetComponent<UnitSoldiers>().control = false;
                GameStatus.statesChoosen = false;
                GameStatus.UnitControl = false;
                GameStatus.ControlReset = false;
                GameStatus.RegControl = gameObject.GetComponent<UnitSoldiers>().numReg;
            }


            // Режим при проверке х-рэй

            Ray ray1 = new Ray(transform.position + new Vector3(0, 2, 0), transform.forward);//forw );
            Ray ray2 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f));
            Ray ray3 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
            Ray ray4 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4));
            Ray ray5 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f * 5 / 4));
            Ray ray6 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f * 3 / 4));


            RaycastHit hit1; RaycastHit hit2; RaycastHit hit3; RaycastHit hit4; RaycastHit hit5; RaycastHit hit6;

            bool temp = Physics.Raycast(ray1, out hit1, 4.0f);
            if (temp == true)
            {
                GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                GameStatus.fr = true;
            }

            temp = Physics.Raycast(ray2, out hit2, 4.0f);
            if (temp == true)
            {
                GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bl = false;
                GameStatus.bk = true;
            }


            temp = Physics.Raycast(ray3, out hit3, 4.0f);
            if (Physics.Raycast(ray3, out hit3, 4.0f) == true)
            {
                GameStatus.fr = GameStatus.fw = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                GameStatus.fl = true;
            }

            temp = Physics.Raycast(ray4, out hit4, 4.0f);
            if (Physics.Raycast(ray4, out hit4, 4.0f) == true)
            {
                GameStatus.fr = GameStatus.fl = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                GameStatus.fw = true;
            }

            if (Physics.Raycast(ray5, out hit5, 4.0f) == true)
            {
                GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bk = false;
                GameStatus.bl = true;
            }


            if (Physics.Raycast(ray6, out hit6, 4.0f) == true)
            {
                GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bl = GameStatus.bk = false;
                GameStatus.bw = true;
            }
        }


     }




    void OnMouseDown()
    {
        if (GameStatus.statesChoosen == true)
        {

            GameStatus.UnitControl = true;
            gameObject.GetComponent<UnitSoldiers>().control = true;

            // Записываем положение камеры
            GameStatus.Cam1 = Camera.main.transform.position;
            GameStatus.Cam2 = Camera.main.transform.rotation;
            //Debug.Log(transform.position);
            Vector3 np = transform.position - transform.forward * 3 + new Vector3(0, 3, 0);
            //Debug.Log(np);
            Camera.main.transform.position = np;
            Camera.main.transform.rotation = transform.rotation;

            if (camera != null)
            {
                //camera.transform.position = gameObject.transform.position;
            }
        }
    }
}
